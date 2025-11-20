#include "tex_gen.h"
#include "GfxTextures.h"

#include "MemoryPool.h"
#include "GraphicsLibPrivate.h"
#include "GfxTextureTools.h"
#include "RdrTexture.h"
#include "earray.h"
#include "file.h"
#include "MemRef.h"
#include "ImageUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);); // Should just be the memory pool and the earrays

typedef struct TexGenQueuedUpdate {
	U32 is_region:1;
	BasicTexture *bind;
	union {
		RdrTexParams *data;
		RdrSubTexParams *subdata;
	};
	U32 image_byte_count_debug;
	U32 tex_is_loading_for; // Since a single bind might have multiple updates queued
} TexGenQueuedUpdate;

static TexGenQueuedUpdate **queuedTexGenUpdates;
static BasicTexture **texgenSharedList;
static BasicTexture **texgenNormalList;
MP_DEFINE(TexGenQueuedUpdate);

// no dst_format needed when updating a sub region
void texGenUpdateRegion_dbg(BasicTexture *bind, U8 *tex_data, int x, int y, int z, int width, int height, int depth, RdrTexType tex_type, RdrTexFormat pixel_format MEM_DBG_PARMS)
{
	U32 image_byte_count;
	bool async_update=false;

	// uncompressed only
	assertmsg(pixel_format == RTEX_BGR_U8 || pixel_format == RTEX_BGRA_U8 || 
		pixel_format == RTEX_R_F32, "Bad pixel format passed to texGenUpdateRegion!");

	assert(bind->tex_handle);

	image_byte_count = imgByteCount(tex_type, pixel_format, width, height, depth, 1);

	assert(bind->rdr_format == pixel_format);

	if (gfx_state.currentDevice->rdr_device->is_locked_nonthread)
	{
		RdrSubTexParams	*rtex;
		rtex = rdrStartUpdateSubTexture(gfx_state.currentDevice->rdr_device, bind->tex_handle, tex_type, pixel_format, x, y, z, width, height, depth, &image_byte_count, NULL);

		memcpy(rtex+1, tex_data, image_byte_count);
		rdrEndUpdateTexture(gfx_state.currentDevice->rdr_device);

		bind->tex_is_loaded |= gfx_state.currentRendererFlag;
	} else {
		async_update = true;
	}

	if (async_update || (bind->flags & TEX_VOLATILE_TEXGEN) && eaSize(&gfx_state.devices) > 1)
	{
		RdrSubTexParams	*rtex;
		TexGenQueuedUpdate *queued_update;
		MP_CREATE(TexGenQueuedUpdate, 32);
		queued_update = MP_ALLOC(TexGenQueuedUpdate);
		queued_update->is_region = 1;
		queued_update->bind = bind;
		queued_update->image_byte_count_debug = image_byte_count;
		queued_update->tex_is_loading_for = gfx_state.allRenderersFlag; // Loading for all renderers
		if (!async_update)
			queued_update->tex_is_loading_for &= ~gfx_state.currentRendererFlag;
		queued_update->subdata = rtex = smalloc(sizeof(*rtex) + image_byte_count);
		ZeroStructForce(rtex);
		rtex->data = rtex+1;
		rtex->width = width;
		rtex->height = height;
		rtex->depth = depth;
		rtex->src_format.format = pixel_format;
		rtex->tex_handle = bind->tex_handle;
		rtex->type = tex_type;
		rtex->x_offset = x;
		rtex->y_offset = y;
		rtex->z_offset = z;
		memcpy(rtex->data, tex_data, image_byte_count);
		eaPush(&queuedTexGenUpdates, queued_update);
	}

}

void texGenUpdate_dbg(BasicTexture *bind, U8 *tex_data, RdrTexType tex_type, RdrTexFormat pixel_format, int levels, bool clamp, bool mirror, bool pointsample, bool refcount_data MEM_DBG_PARMS)
{
	U32 image_byte_count;
	bool async_update=false;
	RdrTexFlags flags = 0;

	if (clamp)
		flags |= RTF_CLAMP_U|RTF_CLAMP_V|RTF_CLAMP_W;
	if (mirror)
		flags |= RTF_MIRROR_U|RTF_MIRROR_V|RTF_MIRROR_W;
	if (pointsample)
		flags |= RTF_MIN_POINT|RTF_MAG_POINT;

	if (bind->tex_handle)
		rdrChangeTexHandleFlags(&bind->tex_handle, flags);
	else
		bind->tex_handle = rdrGenTexHandle(flags);

	texRecordNewMemUsage(bind, TEX_MEM_VIDEO, getTextureMemoryUsageEx(tex_type, pixel_format, bind->realWidth, bind->realHeight, texGetDepth(bind), levels>1, false, false));

	bind->rdr_format = pixel_format;

	if (gfx_state.currentDevice && gfx_state.currentDevice->rdr_device->is_locked_nonthread)
	{
		// Update now!
		RdrTexParams	*rtex;
	
		rtex = rdrStartUpdateTexture(gfx_state.currentDevice->rdr_device, bind->tex_handle, tex_type, pixel_format, bind->realWidth, bind->realHeight, texGetDepth(bind), levels, &image_byte_count, texMemMonitorNameFromFlags(bind->use_category), refcount_data?tex_data:NULL);

		rtex->need_sub_updating = 1;
		rtex->is_srgb = gfxFeatureEnabled(GFEATURE_LINEARLIGHTING);

		if (!rtex->refcount_data)
			memcpy(rtex->data, tex_data, image_byte_count);

		rdrEndUpdateTexture(gfx_state.currentDevice->rdr_device);

		bind->tex_is_loaded |= gfx_state.currentRendererFlag;
	} else {
		image_byte_count = imgByteCount(tex_type, pixel_format, bind->realWidth, bind->realHeight, texGetDepth(bind), levels);
		async_update = true;
	}

	if (async_update || (bind->flags & TEX_VOLATILE_TEXGEN) && eaSize(&gfx_state.devices) > 1) {
		TexGenQueuedUpdate *queued_update;
		RdrTexParams	*rtex;
		MP_CREATE(TexGenQueuedUpdate, 32);
		queued_update = MP_ALLOC(TexGenQueuedUpdate);
		queued_update->is_region = 0;
		queued_update->bind = bind;
		queued_update->image_byte_count_debug = image_byte_count;
		queued_update->tex_is_loading_for = gfx_state.allRenderersFlag; // Loading for all renderers
		if (!async_update)
			queued_update->tex_is_loading_for &= ~gfx_state.currentRendererFlag;
		if (refcount_data)
		{
			int i;
			queued_update->data = rtex = scalloc(1, sizeof(*rtex));
			rtex->data = tex_data;
			rtex->refcount_data = 1;
			for (i = 0; i < eaSize(&gfx_state.devices); ++i)
			{
				if (queued_update->tex_is_loading_for & (1 << i))
					memrefIncrement(tex_data);
			}
		}
		else
		{
			queued_update->data = rtex = smalloc(sizeof(*rtex) + image_byte_count);
			ZeroStructForce(rtex);
			rtex->data = rtex + 1;
			rtex->refcount_data = 0;
			memcpy(rtex->data, tex_data, image_byte_count);
		}
		rtex->tex_handle = bind->tex_handle;
		rtex->type = tex_type;
		rtex->src_format.format = pixel_format;
		rtex->width = bind->realWidth;
		rtex->height = bind->realHeight;
		rtex->depth = texGetDepth(bind);
		rtex->first_level = 0;
		rtex->level_count = levels;
		rtex->max_levels = levels;
		rtex->need_sub_updating = 1;
		eaPush(&queuedTexGenUpdates, queued_update);
	}
}

void texGenUpdateFromWholeSurface(BasicTexture *bind, RdrSurface *src_surface, RdrSurfaceBuffer buffer_num)
{
	RdrTexFormat pixel_format = RTEX_BGRA_U8;
	U32 image_byte_count;
	RdrTexParams	*rtex;
	RdrTexFlags flags = RTF_CLAMP_U|RTF_CLAMP_V;

	assert(!(bind->flags & TEX_VOLATILE_TEXGEN)); // Not valid if you're calling texGenUpdateFromScreen/Surface!

	if (bind->tex_handle)
		rdrChangeTexHandleFlags(&bind->tex_handle, flags);
	else
		bind->tex_handle = rdrGenTexHandle(flags);

	rtex = rdrStartUpdateTextureFromSurface(gfx_state.currentDevice->rdr_device, bind->tex_handle, pixel_format, src_surface, buffer_num, -1, -1, &image_byte_count);

	bind->width = src_surface->width_nonthread;
	bind->height = src_surface->height_nonthread;

	rtex->need_sub_updating = 1;

	rdrEndUpdateTexture(gfx_state.currentDevice->rdr_device);

	bind->tex_is_loaded |= gfx_state.currentRendererFlag;

	texRecordNewMemUsage(bind, TEX_MEM_VIDEO, getTextureMemoryUsageEx(RTEX_2D, pixel_format, bind->width, bind->height, 1, false, false, false));
}

BasicTexture *texGenNewEx(int width, int height, int depth, const char *name, TexGenMode tex_gen_mode, WLUsageFlags use_category)
{
	BasicTexture *basicBind = basicTextureCreate();
	char buf[1024];

	sprintf(buf, "AUTOGEN_TEX - %s", name);
	
	if (tex_gen_mode == TEXGEN_VOLATILE_SHARED) {
		eaPush(&texgenSharedList, basicBind);
		basicBind->flags |= TEX_VOLATILE_TEXGEN;
	} else {
		eaPush(&texgenNormalList, basicBind);
	}
	basicBind->flags |= TEX_TEXGEN;

	basicBind->tex_handle = 0; // rdrGenTexHandle();
	basicBind->actualTexture = basicBind;
	texAllocLoadedData(basicBind)->tex_is_loading_for = 0; // We maintain our own who we're loading for bits
	basicBind->name = allocAddString(buf);
	basicBind->fullname = basicBind->name;
	basicBind->width = basicBind->realWidth = width;
	basicBind->height = basicBind->realHeight = height;
	/*basicBind->depth =*/ texAllocRareData(basicBind)->realDepth = depth;
	basicBind->use_category = use_category;

	return basicBind;
}

BasicTexture *texGenNew(int width, int height, const char *name, TexGenMode tex_gen_mode, WLUsageFlags use_category)
{
	return texGenNewEx(width, height, 0, name, tex_gen_mode, use_category);
}

void texGenFinalizeFree(BasicTexture *bind)
{
	// Gets called after the data has been freed from GL on all renderers
	basicTextureDestroy(bind);
}

BasicTexture **tex_gen_freed_last_frame=NULL;
BasicTexture **tex_gen_freed_this_frame=NULL;
void texGenFreeNextFrame(BasicTexture *bind)
{
	if (!bind)
		return;
	eaPush(&tex_gen_freed_this_frame, bind);
}

void texGenDoDelayFree(void)
{
	int i;
	if(	eaSize(&tex_gen_freed_last_frame) == 0 &&
		eaSize(&tex_gen_freed_this_frame) == 0 )
		return;
	for( i=0; i < eaSize(&tex_gen_freed_last_frame); i++ ) {
		texGenFree(tex_gen_freed_last_frame[i]);
	}
	eaClear(&tex_gen_freed_last_frame);
	eaCopy(&tex_gen_freed_last_frame, &tex_gen_freed_this_frame);
	eaClear(&tex_gen_freed_this_frame);
}

static void freeQueuedUpdate(TexGenQueuedUpdate *queued_update)
{
	if (queued_update->is_region)
	{
		if (queued_update->subdata->refcount_data)
			memrefDecrement(queued_update->subdata->data);
		SAFE_FREE(queued_update->subdata);
	}
	else
	{
		if (queued_update->data->refcount_data)
			memrefDecrement(queued_update->data->data);
		SAFE_FREE(queued_update->data);
	}
	MP_FREE(TexGenQueuedUpdate, queued_update);
}

void texGenFree(BasicTexture *bind)
{
	int i;

	if (!bind)
		return;

	eaFindAndRemoveFast(&texgenSharedList, bind);
	eaFindAndRemoveFast(&texgenNormalList, bind);

	for (i=0; i<eaSize(&queuedTexGenUpdates); i++) {
		TexGenQueuedUpdate *queued_update = queuedTexGenUpdates[i];
		if (queued_update->bind == bind) {
			if (isDevelopmentMode() && UserIsInGroup("Software") && (queued_update->tex_is_loading_for & 1)) { // Not unlikely that secondary device is hidden or running at a reduced framerate, and may not have had this uploaded yet, but it's a problem on the main device
				devassertmsg(0, "Freeing texgen texture before it has been sent to the graphics card, this is probably not what you want.");
			}
			bind->tex_is_loaded |= gfx_state.allRenderersFlag;
			freeQueuedUpdate(queued_update);
			eaRemove(&queuedTexGenUpdates, i);
			i--;
		}
	}

	texFree(bind, 0);
}

void texGenDoFrame(void)
{
	int num_sent_this_frame=0;
	int num_freed_this_frame=0;
	int i;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	assert(gfx_state.currentDevice->rdr_device->is_locked_nonthread);
	for (i=0; i<eaSize(&queuedTexGenUpdates); i++) {
		TexGenQueuedUpdate *queued_update = queuedTexGenUpdates[i];
		if (queued_update->tex_is_loading_for & gfx_state.currentRendererFlag) {
			U32 image_byte_count;
			num_sent_this_frame++;
			// Send to this renderer!
			if (queued_update->is_region) {
				RdrSubTexParams	*data = queued_update->subdata;
				RdrSubTexParams	*rtex;

				rtex = rdrStartUpdateSubTexture(gfx_state.currentDevice->rdr_device, data->tex_handle, data->type, data->src_format.format, data->x_offset, data->y_offset, data->z_offset, data->width, data->height, data->depth, &image_byte_count, data->refcount_data?data->data:NULL);
				assert(image_byte_count == queued_update->image_byte_count_debug);
				if (!data->refcount_data)
					memcpy(rtex->data, data->data, image_byte_count);
				rdrEndUpdateTexture(gfx_state.currentDevice->rdr_device);
			} else {
				RdrTexParams	*data = queued_update->data;
				RdrTexParams	*rtex;

				rtex = rdrStartUpdateTextureEx(gfx_state.currentDevice->rdr_device, data, data->tex_handle, data->type, data->src_format.format, data->width, data->height, data->depth, data->max_levels, &image_byte_count, texMemMonitorNameFromFlags(queued_update->bind->use_category), data->refcount_data?data->data:NULL, 0);
				rtex->need_sub_updating = data->need_sub_updating;
				rtex->is_srgb = gfxFeatureEnabled(GFEATURE_LINEARLIGHTING);
				assert(image_byte_count == queued_update->image_byte_count_debug);
				if (!data->refcount_data)
					memcpy(rtex->data, data->data, image_byte_count);
				rdrEndUpdateTexture(gfx_state.currentDevice->rdr_device);
			}
			queued_update->tex_is_loading_for &=~gfx_state.currentRendererFlag;
			queued_update->bind->tex_is_loaded |= gfx_state.currentRendererFlag;
		}
		if (!queued_update->tex_is_loading_for)
		{
			num_freed_this_frame++;
			// No renderers left
			freeQueuedUpdate(queued_update);
			eaRemove(&queuedTexGenUpdates, i);
			i--;
		}
	}

	PERFINFO_AUTO_STOP();
}

void texGenClearAllForDevice(int rendererIndex)
{
	int i;
	int flag = (1 << rendererIndex);
	for (i=eaSize(&texgenSharedList)-1; i>=0; i--) {
		BasicTexture *bind = texgenSharedList[i];
		if (bind->tex_is_loaded & flag) {
			bind->tex_is_loaded &= ~flag;
		}
		if (bind->loaded_data)
			bind->loaded_data->tex_is_loading_for &= ~flag;
	}
	for (i=eaSize(&texgenNormalList)-1; i>=0; i--) {
		BasicTexture *bind = texgenNormalList[i];
		if (bind->tex_is_loaded & flag) {
			bind->tex_is_loaded &= ~flag;
		}
		if (bind->loaded_data)
			bind->loaded_data->tex_is_loading_for &= ~flag;
	}
	for (i=eaSize(&queuedTexGenUpdates)-1; i>=0; i--) {
		TexGenQueuedUpdate *queued_update = queuedTexGenUpdates[i];
		queued_update->tex_is_loading_for &= ~flag;
	}
}

void texGenDestroyVolatile(void)
{
	int i;
	for (i=eaSize(&texgenSharedList)-1; i>=0; i--) {
		BasicTexture *bind = texgenSharedList[i];
		texGenFree(bind);
	}
	eaSetSize(&texgenSharedList, 0);
	for (i=eaSize(&queuedTexGenUpdates)-1; i>=0; i--) {
		TexGenQueuedUpdate *queued_update = queuedTexGenUpdates[i];
		SAFE_FREE(queued_update->data);
		MP_FREE(TexGenQueuedUpdate, queued_update);
	}
	eaSetSize(&queuedTexGenUpdates, 0);
}

// index should be -1 for all, or between 0 and TEX_NUM_DIVISIONS-1
void texGenForEachTexture(TextureCallback callback, void *userData, int index)
{
	BasicTexture **list[] = {texgenNormalList, texgenSharedList};
	int j;
	for (j=0; j<ARRAY_SIZE(list); j++) {
		int i;
		int total = eaSize(&(list[j]));
		int chunkSize = (total/TEX_NUM_DIVISIONS);
		int iStart = (index==-1)?0:(index*chunkSize);
		int iEnd = (index==-1)?total:(index==TEX_NUM_DIVISIONS-1)?total:((index+1)*chunkSize);
		for (i=iStart; i<iEnd; i++)
			callback((list[j])[i], userData);
	}
}


