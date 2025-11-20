
#include "ogl.h"
#include "rt_tex.h"
#include "rt_state.h"
#include "MemoryPool.h"

// no need for a separate critical section for this, since it is only accessed when a device is locked
MP_DEFINE(RdrTextureDataWinGL);

__forceinline static GLenum getTargetFromType(RdrTexType type)
{
	switch (type)
	{
		xcase RTEX_1D:
			return GL_TEXTURE_1D;

		xcase RTEX_2D:
			return GL_TEXTURE_2D;

		xcase RTEX_CUBEMAP:
			assertmsg(0, "Cubemaps not implemented yet.");

		xdefault:
			assertmsg(0, "Unknown texture type.");
	}

	return GL_TEXTURE_2D;
}

__forceinline static void getSrcFormat(RdrTexFormat src_format_in, RdrTexFormat dst_format_in, GLenum *src_format, GLenum *src_type, U32 *compressed, U32 *block_size)
{
	switch (src_format_in)
	{
		xcase RTEX_BGR_U8:
			*src_format = GL_BGR;
			*src_type = GL_UNSIGNED_BYTE;
			*block_size = 3;

		xcase RTEX_BGRA_U8:
			*src_format = GL_BGRA;
			*src_type = GL_UNSIGNED_BYTE;
			*block_size = 4;

		xcase RTEX_RGBA_F32:
			*src_format = GL_RGBA;
			*src_type = GL_FLOAT;
			*block_size = 16;

		xcase RTEX_R_F32:
			*src_format = GL_LUMINANCE;
			*src_type = GL_FLOAT;
			*block_size = 4;

		xcase RTEX_LA_U8:
			*src_format = GL_LUMINANCE_ALPHA;
			*src_type = GL_UNSIGNED_BYTE;
			*block_size = 2;

		xcase RTEX_DXT1:
			assert(src_format_in == dst_format_in);
			*compressed = 1;
			*src_format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
			*block_size = 8;

		xcase RTEX_DXT3:
			assert(src_format_in == dst_format_in);
			*compressed = 1;
			*src_format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
			*block_size = 16;

		xcase RTEX_DXT5:
			assert(src_format_in == dst_format_in);
			*compressed = 1;
			*src_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			*block_size = 16;

		xdefault:
			assertmsg(0, "Unknown source texture format.");
	}
}

void rwglSetTextureDataDirect(RdrDeviceWinGL *device, RdrTexParams *rtex)
{
	RdrTextureDataWinGL *tex_data = NULL;
	int sub_image = 1;
	GLenum	target, src_format, src_type, dst_format = 0;
	U32		width = rtex->width, height = rtex->height, compressed = 0, block_size = 0;
	U8		*bitmap = (U8*)(rtex+1);

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	if (rtex->from_surface)
		assert(rtex->type == RTEX_2D);

	target = getTargetFromType(rtex->type);

	if (!rtex->from_surface)
		getSrcFormat(rtex->src_format, rtex->dst_format, &src_format, &src_type, &compressed, &block_size);

	switch (rtex->dst_format)
	{
		xcase RTEX_BGR_U8:
			dst_format = GL_RGB8;

		xcase RTEX_BGRA_U8:
			dst_format = GL_RGBA8;

		xcase RTEX_RGBA_F16:
		case RTEX_RGBA_F32:
		case RTEX_R_F32:
			dst_format = GL_RGBA_FLOAT16_ATI;

		xcase RTEX_LA_U8:
			dst_format = GL_LUMINANCE8_ALPHA8;

		xcase RTEX_DXT1:
		case RTEX_DXT3:
		case RTEX_DXT5:
			assert(rtex->src_format == rtex->dst_format);

		xcase RTEX_DEPTH:
			dst_format = GL_DEPTH_COMPONENT;
			assert(0); // Not supported anymore, doesn't consistently work on ATI

		xdefault:
			assertmsg(0, "Unknown destination texture format.");
	}

	assert(rtex->tex_handle > 0);
	stashIntFindPointer(device->texture_data, rtex->tex_handle, &tex_data);
	if (tex_data && (tex_data->width != width || tex_data->height != height || 
		tex_data->compressed != compressed ||
		tex_data->tex_format != dst_format))
	{
		sub_image = 0;
	}
	else if (!tex_data)
	{
		MP_CREATE(RdrTextureDataWinGL, 1024);
		tex_data = MP_ALLOC(RdrTextureDataWinGL);
		stashIntAddPointer(device->texture_data, rtex->tex_handle, tex_data, true);
		sub_image = 0;
	}

	if (sub_image && !tex_data->can_sub_update)
		sub_image = 0;

	rwglBindTexture(target, 0, rtex->tex_handle);

	if (!sub_image)
	{
		// setup texture flags
		if (rtex->clamp_s)
			glTexParameterf(target, GL_TEXTURE_WRAP_S, gl_clamp_val);
		if (rtex->clamp_t)
			glTexParameterf(target, GL_TEXTURE_WRAP_T, gl_clamp_val);
		if (rtex->mirror_s)
			glTexParameterf(target, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT_IBM);
		if (rtex->mirror_t)
			glTexParameterf(target, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT_IBM);
		if (rtex->magfilter_point)
		{
			glTexParameterf(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		} else {
			glTexParameterf(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		if (rtex->minfilter_point)
		{
			glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		}
		else
		{
			if (rtex->mipmap)
				glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			else
				glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			assert(rtex->anisotropy);
			if (rdr_caps.supports_anisotropy)
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, rtex->anisotropy);
		}
		CHECKGL;
		tex_data->compressed = !!compressed;
		tex_data->can_sub_update = !!rtex->need_sub_updating;
	}

	// download the bitmap data
	if (rtex->from_surface)
	{
		RdrSurfaceBuffer src_buffer_num;
		int src_set_index;
		RdrSurfaceWinGL *src_surface;
		src_surface = (RdrSurfaceWinGL*)rdrGetSurfaceForTexHandle(rtex->src_tex_handle, &src_buffer_num, &src_set_index);

		assert(device->active_surface == src_surface); // This code only works if we're copying from the active surface, change this to be like DirectX!

		if (sub_image)
			glCopyTexSubImage2D(target, 0, 0, 0, rtex->x, rtex->y, rtex->width, rtex->height);
		else
			glCopyTexImage2D(target, 0, dst_format, rtex->x, rtex->y, rtex->width, rtex->height, 0);
		CHECKGL;
	}
	else if (compressed)
	{
		// DDS source data (BGRA)
		int	size;
		U32 i;

		for (i = 0; i <= rtex->mip_count && (width || height); ++i)
		{
			if (width == 0)
				width = 1;
			if (height == 0)
				height = 1;

			//compressed rgba
			size = ((width+3)/4) * ((height+3)/4) * block_size;
			glCompressedTexImage2DARB(target, i, src_format, width, height, 0, size, bitmap);
			CHECKGL;

			bitmap += size;
			width  >>= 1;
			height >>= 1;
		}
	}
	else
	{
		int	size;
		U32 i;

		// Raw data
		if (!rtex->mipmap)
			rtex->mip_count = 0;
		for (i = 0; i <= rtex->mip_count && (width || height); ++i)
		{
			if (width == 0)
				width = 1;
			if (height == 0)
				height = 1;

			size = width * height * block_size;
			glTexImage2D(target, i, dst_format, width, height, 0, src_format, src_type, bitmap);
			CHECKGL;

			bitmap += size;
			width  >>= 1;
			height >>= 1;
		}
	}
}

void rwglSetTextureSubDataDirect(RdrDeviceWinGL *device, RdrSubTexParams *rtex)
{
	RdrTextureDataWinGL *tex_data = NULL;
	GLenum	target, src_format, src_type;
	U32		width = rtex->width, height = rtex->height, compressed = 0, block_size = 0;
	U8		*bitmap = (U8*)(rtex+1);

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	target = getTargetFromType(rtex->type);

	assert(rtex->tex_handle > 0);
	stashIntFindPointer(device->texture_data, rtex->tex_handle, &tex_data);
	assert(tex_data);
	assert(tex_data->can_sub_update);

	rwglBindTexture(target, 0, rtex->tex_handle);

	//glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	CHECKGL;

	getSrcFormat(rtex->src_format, rtex->src_format, &src_format, &src_type, &compressed, &block_size);

	// download the bitmap data
	if (compressed)
	{
		// DDS source data (BGRA)
		int	size;

		//compressed rgba
		size = ((width+3)/4)*((height+3)/4)*block_size;
		glCompressedTexSubImage2DARB(target, 0, rtex->x_offset, rtex->y_offset, width, height, src_format, size, bitmap);
		CHECKGL;
	}
	else
	{
		glTexSubImage2D(target, 0, rtex->x_offset, rtex->y_offset, rtex->width, rtex->height, src_format, src_type, bitmap);
		CHECKGL;
	}
}

void rwglGetTexInfoDirect(RdrDeviceWinGL *device, RdrGetTexInfo *get)
{
	GLenum target = getTargetFromType(get->type);
	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	assert(get->type == RTEX_1D || get->type == RTEX_2D);

	rwglBindTexture(target, 0, get->tex_handle);

	glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, get->widthout);
	glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, get->heightout);

	if (get->data)
	{
		GLenum format, type;
		getSrcFormat(get->src_format, -1, &format, &type, 0, 0);
		*get->data = malloc(getImageByteCount(get->type, get->src_format, *get->widthout, *get->heightout, 1));
		glGetTexImage(target, 0, format, type, *get->data);
	}
}


void rwglSetTexAnisotropyDirect(RdrDeviceWinGL *device, RdrTextureAnisotropy *params)
{
	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	if (!rdr_caps.supports_anisotropy)
		return;

	MIN1(params->anisotropy, rdr_caps.maxTexAnisotropic);

	rwglBindTexture(GL_TEXTURE_2D, 0, params->tex_handle);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, params->anisotropy);
}

void rwglFreeTextureDirect(RdrDeviceWinGL *device, TexHandle tex)
{
	RdrTextureDataWinGL *tex_data = NULL;

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	if (tex < 0)
		return;

	if (stashIntRemovePointer(device->texture_data, tex, &tex_data)) {
		MP_FREE(RdrTextureDataWinGL, tex_data);
	} else {
		// Probably a surface/PBuffer texture handle
	}
	glDeleteTextures(1, (U32 *)(&tex));
	rwglResetTextureState();
}

void rwglFreeAllTexturesDirect(RdrDeviceWinGL *device)
{
	StashElement elem;
	StashTableIterator iter;

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	stashGetIterator(device->texture_data, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		TexHandle tex = stashElementGetIntKey(elem);
		RdrTextureDataWinGL *tex_data = stashElementGetPointer(elem);

		glDeleteTextures(1, (U32 *)(&tex));
		MP_FREE(RdrTextureDataWinGL, tex_data);
	}

	stashTableClear(device->texture_data);
	rwglResetTextureState();
}








