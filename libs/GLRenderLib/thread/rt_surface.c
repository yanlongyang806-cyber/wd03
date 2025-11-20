#include "timing.h"
#include "error.h"
#include "file.h"
#include "color.h"

#include "ogl.h"
#include "rt_surface.h"
#include "rt_tex.h"
#include "RdrState.h"
#include "GLRenderLib.h"
#include "systemspecs.h"

static int texTarget = GL_TEXTURE_2D;

__forceinline static int bitsFromSBT(RdrSurfaceBufferType type)
{
	switch (type & SBT_TYPE_MASK)
	{
	xcase SBT_RGBA:
	case SBT_RG_FIXED:
	case SBT_RG_FLOAT:
	case SBT_FLOAT:
		return 32;

	xcase SBT_RGBA_FIXED:
	case SBT_RGBA_FLOAT:
		return 64;
	}

	return 0;
}

void rwglFreeSurfaceDataDirect(RdrDeviceWinGL *device, RdrSurfaceWinGL *surface);

void rwglSetSurfaceFogDirect(RdrSetFogData *data)
{
	RdrSurfaceWinGL *glsurface = (RdrSurfaceWinGL *)data->surface;
	rwglFogRange(&glsurface->state, data->near_dist, data->far_dist);
	rwglFogColor(&glsurface->state, data->fog_color);
}

void rwglUpdateSurfaceMatricesDirect(RwglUpdateMatrixData *data)
{
	rwglSet3DProjection(&data->surface->state, data->projection);
	rwglSetViewMatrix(&data->surface->state, data->view_mat, data->inv_view_mat);
}

static void rwglSetAppropriateDrawBuffers(RdrSurfaceWinGL *surface)
{
	if (surface->creation_flags & SF_MRT4)
	{
		U32 i;
		int buffers[10] = {0};
		GLenum aux_base;
		if (surface->type == SURF_FBO) {
			buffers[0] = GL_COLOR_ATTACHMENT0_EXT;
			aux_base = GL_COLOR_ATTACHMENT1_EXT;
		} else {
			buffers[0] = GL_FRONT_LEFT;
			aux_base = GL_AUX0;
		}
		for (i=0; i<3; i++) {
			buffers[1+i] = aux_base+i;
		}
		glDrawBuffersARB(4, buffers);
		CHECKGL;
	}
	else if (surface->creation_flags & SF_MRT2)
	{
		U32 i;
		int buffers[10] = {0};
		GLenum aux_base;
		if (surface->type == SURF_FBO) {
			buffers[0] = GL_COLOR_ATTACHMENT0_EXT;
			aux_base = GL_COLOR_ATTACHMENT1_EXT;
		} else {
			buffers[0] = GL_FRONT_LEFT;
			aux_base = GL_AUX0;
		}
		for (i=0; i<1; i++) {
			buffers[1+i] = aux_base+i;
		}
		glDrawBuffersARB(2, buffers);
		CHECKGL;
	}
}

void rwglClearActiveSurfaceDirect(RdrDeviceWinGL *device, RdrClearParams *params)
{
	int gl_clear_flags=0;

	CHECKDEVICELOCK(device);
	CHECKGLTHREAD;
	
	if (params->bits & CLEAR_STENCIL)
	{
		gl_clear_flags |= GL_STENCIL_BUFFER_BIT;
		glClearStencil(0);
		CHECKGL;
	}
	if (params->bits & CLEAR_DEPTH)
	{
		gl_clear_flags |= GL_DEPTH_BUFFER_BIT;
		glClearDepth(params->clear_depth);
		CHECKGL;
	}
	if (params->bits & CLEAR_COLOR)
	{
		gl_clear_flags |= GL_COLOR_BUFFER_BIT;
		glClearColor(params->clear_color[0], params->clear_color[1], params->clear_color[2], params->clear_color[3]); 
		CHECKGL;
	}
	glClear(gl_clear_flags);
// Debug code to make sure multiple render targets work:
//	if (device->active_surface->num_aux_buffers) {
//		glClearColor(1, 0, 0, 1);
//		glDrawBuffer(GL_AUX0);
//		glClear(GL_COLOR_BUFFER_BIT);
//		glClearColor(0, 1, 0, 1);
//		glDrawBuffer(GL_AUX1);
//		glClear(GL_COLOR_BUFFER_BIT);
//		rwglSetAppropriateDrawBuffers(device->active_surface);
//	}
	CHECKGL;
}

//////////////////////////////////////////////////////////////////////////
__forceinline static void setTextureParameters()
{
	glTexParameterf(texTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(texTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(texTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(texTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
}

static void rwglMakeCurrentSafeDirect(HDC hDC, HGLRC glRC, F32 timeout)
{
	int num_failures=0;
	int timer=0;

	if (!hDC || !glRC)
		return;

	while (!wglMakeCurrent(hDC, glRC))
	{
		num_failures++;
		if (!timer)
			timer = timerAlloc();
		if (timerElapsed(timer)>timeout) {
			FatalErrorf("wglMakeCurrent failed for %f seconds", timeout);
		}
		Sleep(1);
	}
	if (timer)
		timerFree(timer);

	CHECKGL;
}

static void updateTextureFromBuffer(RdrSurfaceWinGL *surface, int *handle, GLenum buffer, bool copyTextureShifted, int x0, int y0, int w, int h, int buffer_num)
{
	bool init=false;
	if (!*handle) {
		init = true;
		glGenTextures(1, handle);
	}
	rwglBindTexture(texTarget, 0, *handle);
	glReadBuffer(buffer);
	CHECKGL;
	if (init) {
		glCopyTexImage2D(texTarget, 0, surface->tex_formats[buffer_num], 0, 0, surface->virtual_width, surface->virtual_height, 0);
		CHECKGL;
	}
	if (!init || copyTextureShifted) {
		glCopyTexSubImage2D(texTarget, 0, x0, y0, x0, y0, w, h);
		CHECKGL;
	}
}

void rwglUnsetSurfaceActiveDirect(RdrDeviceWinGL *device)
{
	RdrSurfaceWinGL *surface = device->active_surface;

	CHECKDEVICELOCK(device);

	if (!surface)
	{
		rwglSetStateActive(0, 0, 0, 0);
		return;
	}

	CHECKGLTHREAD;

	// We are done rendering to this PBuffer.  If it's a fake RTT PB, copy it now!
	{
		int x0, y0, w, h, x0dest, y0dest;
		bool copyTextureShifted=false;
		x0 = y0 = x0dest = y0dest = 0;
		w = surface->width;
		h = surface->height;
		if (rdr_caps.copy_subimage_from_rtt_invert_hack)
		{
			if (surface->type != SURF_FAKE_PBUFFER && !surface->is_faked_RTT)
			{
				y0dest = y0 = surface->virtual_height - h;
				copyTextureShifted = true;
			}
			else if (surface->type == SURF_FBO)
			{
				y0 = 0;
				y0dest = surface->virtual_height - h;
				copyTextureShifted = true;
			}
		}

		if (surface->is_faked_RTT && surface->type != SURF_FBO)
		{
			updateTextureFromBuffer(surface, &surface->tex_handle, GL_FRONT_LEFT, copyTextureShifted, x0, y0, w, h, 0);
			if (surface->creation_flags & SF_MRT4)
			{
				U32 i;
				for (i=0; i<3; i++) {
					updateTextureFromBuffer(surface, &surface->aux_handles[i], GL_AUX0+i, copyTextureShifted, x0, y0, w, h, i+1);
				}
				glReadBuffer(GL_FRONT_LEFT);
				CHECKGL;
			}
			else if (surface->creation_flags & SF_MRT2)
			{
				U32 i;
				for (i=0; i<1; i++) {
					updateTextureFromBuffer(surface, &surface->aux_handles[i], GL_AUX0+i, copyTextureShifted, x0, y0, w, h, i+1);
				}
				glReadBuffer(GL_FRONT_LEFT);
				CHECKGL;
			}
		}

		if (surface->need_depth_texture && (surface->type != SURF_FBO || surface->fbo.depth_rbhandle))
		{
			// Need the depth texture
			bool init=false;
			if (!surface->depth_handle)
			{
				init = true;
				surface->depth_handle = rdrGenTexHandle();
			}
			PERFINFO_AUTO_START("PBuffer:DepthBuffer", 1);
			rwglBindTexture(texTarget, 0, surface->depth_handle);
			if (init)
			{
				setTextureParameters();
				glCopyTexImage2D(texTarget, 0, GL_DEPTH_COMPONENT, 0, 0, surface->virtual_width, surface->virtual_height, 0);
				CHECKGL;
			}
			if (!init || copyTextureShifted)
			{
				if (rdr_caps.copy_subimage_shifted_from_fbos_hack) {
					glCopyTexSubImage2D(texTarget, 0, x0dest, y0dest, x0, y0, w, h);
				} else {
					glCopyTexSubImage2D(texTarget, 0, 0, 0, 0, 0, surface->virtual_width, surface->virtual_height);
				}
				CHECKGL;
			}
			PERFINFO_AUTO_STOP();
		}

		if (surface->type == SURF_FBO)
		{
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
			CHECKGL;
		}
	}

	device->active_surface = 0;
	rwglSetStateActive(0, 0, 0, 0);
}

void rwglSetSurfaceActiveDirect(RdrDeviceWinGL *device, RdrSurfaceWinGL *surface)
{
	RdrSurfaceWinGL *prev_active_surface = device->active_surface;

	CHECKDEVICELOCK(device);

	if (!surface)
	{
		rwglUnsetSurfaceActiveDirect(device);
		return;
	}

	assert(surface->surface_base.device == ((RdrDevice *)device));

	if (device->active_surface == surface)
		return;

	if (!device->primary_surface.primary.glRC || !device->primary_surface.primary.hDC)
		return;

	if (device->active_surface)
		rwglUnsetSurfaceActiveDirect(device);

	switch (surface->type)
	{
		xcase SURF_PRIMARY:
			rwglMakeCurrentSafeDirect(surface->primary.hDC, surface->primary.glRC, 15);

		xcase SURF_FAKE_PBUFFER:
			rwglMakeCurrentSafeDirect(device->primary_surface.primary.hDC, device->primary_surface.primary.glRC, 15);

		xcase SURF_PBUFFER:
			rwglMakeCurrentSafeDirect(surface->pbuffer.hDC, surface->pbuffer.glRC, 2);
			rwglSetAppropriateDrawBuffers(surface);

		xcase SURF_FBO:
			if (prev_active_surface != &device->primary_surface)
				rwglMakeCurrentSafeDirect(device->primary_surface.primary.hDC, device->primary_surface.primary.glRC, 15);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surface->fbo.handle);
			CHECKGL;
			rwglSetAppropriateDrawBuffers(surface);

		xdefault:
			assert(0);
	}

	device->active_surface = surface;

	rwglSetStateActive(&surface->state, !surface->state_inited, surface->width, surface->height);
	surface->state_inited = 1;

	CHECKGLTHREAD;
}

//////////////////////////////////////////////////////////////////////////

// Null-terminated arrays of pairs
__forceinline static int a2cmp(int *a0, int *a1)
{
	while(*a0==*a1 && *a0!=0) {
		a0++; a1++;
		if (*a0!=*a1)
			return *a0-*a1;
		a0++; a1++;
	}
	return *a0-*a1;
}

__forceinline static int a2len(int *a)
{
	int i;
	for (i=0; a[i]!=0; i+=2);
	return i;
}

__forceinline static int *a2dup(int *a)
{
	int len = (a2len(a)+2)*sizeof(int);
	int *ret = malloc(len);
	memcpy(ret, a, len);
	return ret;
}

__forceinline static void choosePixelFormatCached(HDC hdc, int *formatAttributes, float *fattributes, int pformat_size, int *pformat, int *nformats, int multisample_level, int num_aux_buffers)
{
	static HDC cached_hdc=0;
	static int num_cached=0;
	static struct {
		int *formatAttributes;
		float *fattributes;
		int pformat;
	} cache[20];
	int i;
	if (hdc != cached_hdc) {
		for (i=0; i<num_cached; i++) {
			SAFE_FREE(cache[i].formatAttributes);
			SAFE_FREE(cache[i].fattributes);
		}
		num_cached = 0;
		cached_hdc = hdc;
	}
	for (i=0; i<num_cached; i++) {
		if (a2cmp(cache[i].formatAttributes, formatAttributes)==0 &&
			a2cmp((int*)cache[i].fattributes, (int*)fattributes)==0)
		{
			*nformats = 1;
			pformat[0] = cache[i].pformat;
			// DEBUG!
			//wglChoosePixelFormatARB( hdc, formatAttributes, fattributes, pformat_size, pformat, nformats );
			//assert(pformat[0]==cache[i].pformat);
			return;
		}
	}

	if ( !wglChoosePixelFormatARB( hdc, formatAttributes, fattributes, pformat_size, pformat, nformats ) ) {
		FatalErrorf("pbuffer creation error:  Couldn't query pixel formats.");
	}
	// Search for closest match
	if ((multisample_level > 1  || num_aux_buffers>=1) && *nformats && wglGetPixelFormatAttribivARB) {
		int bestFormat = 0;
		int minDiff = 0x7FFFFFFF;
		int attribs[] = {WGL_SAMPLES_ARB, WGL_AUX_BUFFERS_ARB};
		int results[ARRAY_SIZE(attribs)];

		// Search for the pixel format with the closest number of multisample samples to the requested
		for (i = 0; i < *nformats; i++){
			int diff;
			wglGetPixelFormatAttribivARB(hdc, pformat[i], 0, ARRAY_SIZE(attribs), attribs, results);
			diff = ABS(results[0] - multisample_level) + ABS(results[1] - num_aux_buffers);
			if (diff < minDiff){
				minDiff = diff;
				bestFormat = i;
			}
		}
		pformat[0] = pformat[bestFormat];
	}

	if (*nformats && num_cached<ARRAY_SIZE(cache)) {
		cache[num_cached].formatAttributes = a2dup(formatAttributes);
		cache[num_cached].fattributes = (float*)a2dup((int*)fattributes);
		cache[num_cached].pformat = pformat[0];
		num_cached++;
	}
}

__forceinline static void checkFBO()
{
	extern char *glErrorFromInt(int v);
	if (glCheckFramebufferStatusEXT)
	{
		GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
		if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
			Errorf("Framebuffer status : 0x%x (%s)\n", status, glErrorFromInt(status));
	}
}

__forceinline static void pbufCalcSizes(int width, int height, int *owidth, int *oheight, int *ovwidth, int *ovheight, bool makePowerOf2, int flags, int depth_bits)
{
	*ovwidth = width;
	*ovheight = height;
	// Pixel buffers do *not* need to be a power of two, unless they have a texture target
	*owidth = *ovwidth;
	*oheight = *ovheight;
	if (makePowerOf2)
	{
		*ovwidth = pow2(*ovwidth);
		*ovheight = pow2(*ovheight);
	}
/*	else if (rdr_caps.supports_arb_np2 && flags & SF_DEPTH_TEXTURE && rdr_caps.videoCardVendorID == VENDOR_NV && isPower2(*ovwidth) && isPower2(*ovheight) && (flags & SF_RENDER_TEXTURE) && depth_bits && rdr_caps.supports_render_texture)
	{
		// Stupid crazy hack because reading the depth texture from a square RTT PBuffer is slow
		*ovwidth+=1;
	}
	*/
}

static void pbufInitFake(RdrSurfaceWinGL *surface, RdrSurfaceParams *params)
{
	surface->hardware_multisample_level = 1;
	surface->desired_multisample_level = params->desired_multisample_level;
	surface->required_multisample_level = 1;
	surface->software_multisample_level = 1;
	if (!rdr_caps.supports_arb_np2)
	{
		surface->virtual_width = pow2(params->width);
		surface->virtual_height = pow2(params->height);
	} else {
		surface->virtual_width = params->width;
		surface->virtual_height = params->height;
	}
	surface->width = params->width;
	surface->height = params->height;
	surface->type = SURF_FAKE_PBUFFER;
	surface->is_faked_RTT = !!(params->flags & SF_RENDER_TEXTURE);
	surface->creation_flags &= ~SF_FBO;
}

static int sameBufferTypes(RdrSurfaceParams *params, RdrSurfaceWinGL *surface)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(params->buffer_types); i++)
	{
		if (surface->buffer_types[i] != (params->buffer_types[i] & SBT_TYPE_MASK))
			return 0;
	}

	return 1;
}

void rwglInitSurfaceDirect(RdrDeviceWinGL *device, RwglSurfaceParams *glparams)
{
	RdrSurfaceWinGL *surface = glparams->surface;
	RdrSurfaceParams *params = &glparams->params;
	HDC hdc;
	HGLRC hglrc;
	int alpha_bits=-1;
	int pformat[256];
	unsigned int nformats;
	int i, multisample_level = params->desired_multisample_level;
	bool makePowerOf2=false;
	int floatDepth=16;

	// Attribute arrays must be "0" terminated
	float fattributes[] = {0,0} ;
	int formatAttributes[50] = {
		WGL_DRAW_TO_PBUFFER_ARB, GL_TRUE,
			WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
			WGL_ACCELERATION_ARB,  WGL_FULL_ACCELERATION_ARB,
			WGL_DOUBLE_BUFFER_ARB, GL_FALSE,
			0,0
	};
	int faIndex;
	int bufferAttributes[50] = {
		WGL_PBUFFER_LARGEST_ARB, GL_TRUE,
			0,0
	};
	int baIndex;
	int multiSampleIndex=0;

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	assert(surface->type != SURF_PRIMARY);

	if (params->flags & SF_MRT4)
	{
		int bits = bitsFromSBT(params->buffer_types[0]);
		for (i = 1; i < 4; i++)
			assert(bits == bitsFromSBT(params->buffer_types[i]));
	}
	else if (params->flags & SF_MRT2)
	{
		int bits = bitsFromSBT(params->buffer_types[0]);
		assert(bits == bitsFromSBT(params->buffer_types[1]));
		params->buffer_types[2] = params->buffer_types[0];
		params->buffer_types[3] = params->buffer_types[0];
	}
	else
	{
		for (i = 1; i < ARRAY_SIZE(params->buffer_types); i++)
			params->buffer_types[i] = params->buffer_types[0];
	}

	// Determine if we need to re-create this pbuffer or just re-use it
	if (surface->type != SURF_UNINITED
		&& params->required_multisample_level<=1
		&& params->desired_multisample_level == surface->desired_multisample_level
		&& params->flags == surface->creation_flags
		&& sameBufferTypes(params, surface)
		&& !rdr_state.pbuftest)
	{
		int newvw, newvh, neww, newh;
		// Already inited and it's a vanilla PBuffer
		if (params->flags & SF_RENDER_TEXTURE)
		{
			// Make resolution a power of 2!
			if (!rdr_caps.supports_arb_np2)
				makePowerOf2 = true;
		}
		pbufCalcSizes(params->width, params->height, &neww, &newh, &newvw, &newvh, makePowerOf2, params->flags, params->depth_bits);
		if (params->flags == surface->creation_flags &&
			surface->virtual_width == newvw &&
			surface->virtual_height == newvh)
		{
			// Re-use it!
			surface->width = neww;
			surface->height = newh;
			return;
		}
	}

	rwglSetSurfaceActiveDirect(device, &device->primary_surface);

	if (surface->type != SURF_UNINITED)
	{
		rwglFreeSurfaceDataDirect(device, surface);
		surface->tex_handle = 0;
		surface->depth_handle = 0;
	}

	surface->creation_flags = params->flags;

	for (i = 0; i < ARRAY_SIZE(params->buffer_types); i++)
	{
		surface->buffer_types[i] = params->buffer_types[i] & SBT_TYPE_MASK;
		switch (surface->buffer_types[i])
		{
			xcase SBT_RGBA:
				surface->tex_formats[i] = GL_RGBA8;
				surface->point_sample[i] = 0;

			xcase SBT_RGBA_FIXED:
				surface->tex_formats[i] = GL_RGBA16;
				surface->point_sample[i] = 1;

			xcase SBT_RGBA_FLOAT:
				surface->tex_formats[i] = GL_RGBA_FLOAT16_ATI;
				surface->point_sample[i] = 1;

			xcase SBT_RG_FIXED:
				surface->tex_formats[i] = GL_LUMINANCE16_ALPHA16;
				surface->point_sample[i] = 1;

			xcase SBT_RG_FLOAT:
				surface->tex_formats[i] = GL_FLOAT_RG16_NV;
				surface->point_sample[i] = 1;

			xcase SBT_FLOAT:
				surface->tex_formats[i] = GL_FLOAT_R32_NV;
				surface->point_sample[i] = 1;
		}

		if (params->buffer_types[i] & SBT_FORCE_POINT_SAMPLE)
			surface->point_sample[i] = 1;
		else if (params->buffer_types[i] & SBT_FORCE_LINEAR_FILTER)
			surface->point_sample[i] = 0;
	}

	if (params->flags & SF_FBO)
	{
		// Init dummy stuff
		pbufInitFake(surface, params);

		surface->creation_flags |= SF_FBO; // Cleared in pbufInitFake
		surface->is_faked_RTT = 0;

		glGenFramebuffersEXT(1, &surface->fbo.handle);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surface->fbo.handle);
		CHECKGL;

		surface->tex_handle = rdrGenTexHandle();

		// initialize color texture
		rwglBindTexture(texTarget, 0, surface->tex_handle);
		setTextureParameters();
		glTexImage2D(texTarget, 0, surface->tex_formats[0], surface->virtual_width, surface->virtual_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		CHECKGL;
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, texTarget, (U32)surface->tex_handle, 0);
		CHECKGL;

		// Initialize Depth texture
		if (params->depth_bits)
		{
			if (rdr_caps.videoCardVendorID == VENDOR_ATI)
			{
				// ATI can't do depth texture
				// depth texture not needed (but still need depth buffer)
				glGenRenderbuffersEXT(1, &surface->fbo.depth_rbhandle);
				glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, surface->fbo.depth_rbhandle);
				glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, surface->virtual_width, surface->virtual_height);
				glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, surface->fbo.depth_rbhandle);
				CHECKGL;
				surface->need_depth_texture = 1;
			}
			else
			{
				// Works on NVidia
				GLint depth_format;
				surface->depth_handle = rdrGenTexHandle();

				if (params->depth_bits == 16)
					depth_format = GL_DEPTH_COMPONENT16_ARB;
				else
					depth_format = GL_DEPTH_COMPONENT24_ARB;

				rwglBindTexture(texTarget, 0, surface->depth_handle);
				setTextureParameters();
				glTexImage2D(texTarget, 0, depth_format, surface->virtual_width, surface->virtual_height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
				CHECKGL;
				glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, texTarget, (U32)surface->depth_handle, 0);
				CHECKGL;
			}
		}

		if (0 && params->stencil_bits)
		{
			// initialize stencil renderbuffer
			glGenRenderbuffersEXT(1, &surface->fbo.stencil_rbhandle);
			glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, surface->fbo.stencil_rbhandle);
			CHECKGL;
			glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_STENCIL_INDEX_EXT, surface->virtual_width, surface->virtual_height);
			CHECKGL;
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, surface->fbo.stencil_rbhandle);
			CHECKGL;
			// not needed? glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
		}

		if (surface->creation_flags & SF_MRT4)
		{
			for (i=0; i<3; i++) 
			{
				// initialize aux renderbuffer
				surface->aux_handles[i] = rdrGenTexHandle();

				// initialize color texture
				rwglBindTexture(texTarget, 0, surface->aux_handles[i]);
				CHECKGL;
				setTextureParameters();
				glTexImage2D(texTarget, 0, surface->tex_formats[i+1], surface->virtual_width, surface->virtual_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
				CHECKGL;
				glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT+i, texTarget, (U32)surface->aux_handles[i], 0);
				CHECKGL;	
			}
		}
		else if (surface->creation_flags & SF_MRT2)
		{
			// initialize aux renderbuffer
			surface->aux_handles[0] = rdrGenTexHandle();

			// initialize color texture
			rwglBindTexture(texTarget, 0, surface->aux_handles[0]);
			CHECKGL;
			setTextureParameters();
			glTexImage2D(texTarget, 0, surface->tex_formats[1], surface->virtual_width, surface->virtual_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			CHECKGL;
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, texTarget, (U32)surface->aux_handles[0], 0);
			CHECKGL;	
		}

		checkFBO();
		CHECKGL;

		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
		CHECKGL;
		rwglBindTexture(texTarget, 0, 0);
		CHECKGL;

		surface->type = SURF_FBO;

		return;
	}


	//////////////////////////////////////////////////////////////////////////
	// PBuffer only

	for (i = 0; i < ARRAY_SIZE(surface->buffer_types); i++)
		surface->buffer_types[i] = SBT_RGBA;

	for (faIndex=0; formatAttributes[faIndex]; faIndex+=2);
	for (baIndex=0; bufferAttributes[baIndex]; baIndex+=2);

	surface->pbuffer.handle = 0;
	hdc = device->primary_surface.primary.hDC;
	hglrc = device->primary_surface.primary.glRC;

	if (params->stencil_bits) {
		formatAttributes[faIndex++] = WGL_STENCIL_BITS_ARB;
		formatAttributes[faIndex++] = params->stencil_bits;
	}
	formatAttributes[faIndex++] = WGL_DEPTH_BITS_ARB;
	formatAttributes[faIndex++] = params->depth_bits;
	formatAttributes[faIndex++] = WGL_ALPHA_BITS_ARB;
	if (surface->buffer_types[0] == SBT_RGBA) {
		formatAttributes[faIndex++] = 8;
	} else {
		formatAttributes[faIndex++] = 0;
	}
	formatAttributes[faIndex++] = WGL_PIXEL_TYPE_ARB;
	formatAttributes[faIndex++] = WGL_TYPE_RGBA_ARB;

	if (surface->creation_flags & SF_MRT4)
	{
		formatAttributes[faIndex++] = WGL_AUX_BUFFERS_ARB;
		formatAttributes[faIndex++] = 3;
	}
	else if (surface->creation_flags & SF_MRT2)
	{
		formatAttributes[faIndex++] = WGL_AUX_BUFFERS_ARB;
		formatAttributes[faIndex++] = 1;
	}

	if (params->flags & SF_RENDER_TEXTURE) {
		if (rdr_caps.supports_render_texture && params->desired_multisample_level<=1) {
			if (surface->buffer_types[0] == SBT_RGBA) {
				formatAttributes[faIndex++] = WGL_BIND_TO_TEXTURE_RGBA_ARB;
			} else {
				formatAttributes[faIndex++] = WGL_BIND_TO_TEXTURE_RGB_ARB;
			}
			formatAttributes[faIndex++] = GL_TRUE;

			bufferAttributes[baIndex++] = WGL_TEXTURE_FORMAT_ARB;
			if (surface->buffer_types[0] == SBT_RGBA) {
				bufferAttributes[baIndex++] = WGL_TEXTURE_RGBA_ARB;
			} else {
				bufferAttributes[baIndex++] = WGL_TEXTURE_RGB_ARB;
			}

			bufferAttributes[baIndex++] = WGL_TEXTURE_TARGET_ARB;
			bufferAttributes[baIndex++] = WGL_TEXTURE_2D_ARB;

			bufferAttributes[baIndex++] = WGL_MIPMAP_TEXTURE_ARB;
			bufferAttributes[baIndex++] = GL_FALSE;
		} else {
			surface->is_faked_RTT = 1;
		}

		// Make resolution a power of 2!
		if (!rdr_caps.supports_arb_np2)
			makePowerOf2 = true;
	}

	if (multisample_level > 1) {
		multiSampleIndex = faIndex;
		// Check For Multisampling
		formatAttributes[faIndex++] = WGL_SAMPLE_BUFFERS_ARB;
		formatAttributes[faIndex++] = GL_TRUE; // ATI 8500/9000 does not support this :(
		formatAttributes[faIndex++] = WGL_SAMPLES_ARB;
		formatAttributes[faIndex++] = multisample_level;
	}

	// Terminate
	bufferAttributes[baIndex++] = 0;
	bufferAttributes[baIndex++] = 0;
	formatAttributes[faIndex++] = 0;
	formatAttributes[faIndex++] = 0;

	// Query for a suitable pixel format based on the specified mode.
	do {
		if (multiSampleIndex) {
			formatAttributes[multiSampleIndex+3] = multisample_level;
			if (multisample_level <= 1) {
				// No multisampling
				formatAttributes[multiSampleIndex+0] = formatAttributes[multiSampleIndex+1] = 0;
			}
		}
		choosePixelFormatCached(hdc, formatAttributes, fattributes, ARRAY_SIZE(pformat), pformat, &nformats, multisample_level, 3);
		if (!nformats)
			multisample_level >>= 1;
	} while (!nformats && multisample_level!=0);

	if (!nformats) {
		if (isProductionMode()) {
			printf("pbuffer creation error:  Couldn't find a suitable pixel format.\n  Falling back to RTBBCTT.\n" );
			pbufInitFake(surface, params);
			return;
		}
		FatalErrorf("pbuffer creation error:  Couldn't find a suitable pixel format.\n" );
	}

	if (surface->creation_flags & SF_MRT4)
	{
		int queryFormatAttributes[] = {WGL_AUX_BUFFERS_ARB};
		int results[ARRAY_SIZE(queryFormatAttributes)];
		wglGetPixelFormatAttribivARB(hdc, pformat[0], 0, ARRAY_SIZE(queryFormatAttributes), queryFormatAttributes, results);
		//printf("# of Aux buffers: %d\n", results[0]);
		assert((U32)results[0] >= 3);
	}
	else if (surface->creation_flags & SF_MRT2)
	{
		int queryFormatAttributes[] = {WGL_AUX_BUFFERS_ARB};
		int results[ARRAY_SIZE(queryFormatAttributes)];
		wglGetPixelFormatAttribivARB(hdc, pformat[0], 0, ARRAY_SIZE(queryFormatAttributes), queryFormatAttributes, results);
		//printf("# of Aux buffers: %d\n", results[0]);
		assert((U32)results[0] >= 1);
	}

	// Create the p-buffer.
	surface->hardware_multisample_level = MAX(multisample_level, 1);
	surface->desired_multisample_level = MAX(params->desired_multisample_level, 1);
	surface->required_multisample_level = MAX(params->required_multisample_level, 1);
	surface->software_multisample_level = MAX(surface->required_multisample_level / surface->hardware_multisample_level, 1);
	pbufCalcSizes(params->width * surface->software_multisample_level, params->height * surface->software_multisample_level, &surface->width, &surface->height, &surface->virtual_width, &surface->virtual_height, makePowerOf2, params->flags, params->depth_bits);
	//printf("Requested: %d, hardware: %d, software: %d, (%dx%d)\n", pbuf->desired_multisample_level, pbuf->hardware_multisample_level, pbuf->software_multisample_level, pbuf->width, pbuf->height);
	{
		int tries=4; // Try multiple times, Intel says this may work on their card when it sometimes fails the first time.
		do {
			tries--;
			surface->pbuffer.handle = wglCreatePbufferARB(hdc, pformat[0], surface->virtual_width, surface->virtual_height, bufferAttributes);
			if (!surface->pbuffer.handle)
			{
				static DWORD err;
				err = GetLastError(); // static so it shows up in optimzed minidumps
				if ( tries==0 )
				{
					if (isProductionMode()) {
						printf("pbuffer creation error:  wglCreatePbufferARB returned NULL (error: 0x%08X).\n  Falling back to RTBBCTT.\n", err );
						pbufInitFake(surface, params);
						return;
					}

					if ( err == ERROR_INVALID_PIXEL_FORMAT )
						FatalErrorf("error:  ERROR_INVALID_PIXEL_FORMAT\n" );
					else if ( err == ERROR_NO_SYSTEM_RESOURCES )
						FatalErrorf("error:  ERROR_NO_SYSTEM_RESOURCES\n" );
					else if ( err == ERROR_INVALID_DATA )
						FatalErrorf("error:  ERROR_INVALID_DATA\n" );
					else {
						char buf[32];
						sprintf_s(SAFESTR(buf), "error:  %08X\n", err);
						FatalErrorf( buf );
					}
				}
			}
		} while (!surface->pbuffer.handle);
	}

	// Get the device context.
	surface->pbuffer.hDC = wglGetPbufferDCARB(surface->pbuffer.handle);
	if (!surface->pbuffer.hDC) // This happens if you ask for too large of a pbuffer
		FatalErrorf("pbuffer creation error:  wglGetPbufferDCARB() failed\n" );

	// Create a gl context for the p-buffer.
	surface->pbuffer.glRC = wglCreateContext(surface->pbuffer.hDC );
	if (!surface->pbuffer.glRC)
		FatalErrorf("pbuffer creation error:  wglCreateContext() failed\n" );

	if (!wglShareLists(hglrc, surface->pbuffer.glRC))
		FatalErrorf("pbuffer: wglShareLists() failed\n" );

	// Determine the actual width and height we were able to create.
	wglQueryPbufferARB(surface->pbuffer.handle, WGL_PBUFFER_WIDTH_ARB, &surface->virtual_width );
	wglQueryPbufferARB(surface->pbuffer.handle, WGL_PBUFFER_HEIGHT_ARB, &surface->virtual_height );
	wglQueryPbufferARB(surface->pbuffer.handle, WGL_SAMPLES_ARB, &surface->hardware_multisample_level);

	// Determine if render to texture is *really* supported
	if ((surface->creation_flags & SF_RENDER_TEXTURE) && !surface->is_faked_RTT)
	{
		GLint texFormat = WGL_NO_TEXTURE_ARB;
		wglQueryPbufferARB(surface->pbuffer.handle, WGL_TEXTURE_FORMAT_ARB, &texFormat);
		if (texFormat == WGL_NO_TEXTURE_ARB)
			surface->is_faked_RTT = true;
	}

	surface->type = SURF_PBUFFER;

	rwglSetSurfaceActiveDirect(device, surface);

	rwglSetAppropriateDrawBuffers(surface);

	glGetIntegerv(GL_DEPTH_BITS, &surface->pbuffer.depth_bits);
	glGetIntegerv(GL_ALPHA_BITS, &alpha_bits);
	CHECKGL;

	if (surface->pbuffer.depth_bits == 16)
		surface->pbuffer.depth_format = GL_DEPTH_COMPONENT16_ARB;
	else
		surface->pbuffer.depth_format = GL_DEPTH_COMPONENT24_ARB;

//	if (surface->creation_flags & SF_DEPTH_TEXTURE)
//		surface->need_depth_texture = 1;

	if (surface->hardware_multisample_level > 1) {
		glEnable(GL_MULTISAMPLE_ARB);
		CHECKGL;
	}

	rwglSetSurfaceActiveDirect(device, &device->primary_surface);
}

static void rwglFreeSurfaceDataDirect(RdrDeviceWinGL *device, RdrSurfaceWinGL *surface)
{
	U32 i;
	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	if (device->active_surface == surface)
		rwglSetSurfaceActiveDirect(device, &device->primary_surface);

	CHECKSURFACENOTACTIVE(surface);

	switch (surface->type)
	{
		xcase SURF_FBO:
			if (surface->fbo.stencil_rbhandle)
				glDeleteRenderbuffersEXT(1, &surface->fbo.stencil_rbhandle);
			if (surface->fbo.depth_rbhandle)
				glDeleteRenderbuffersEXT(1, &surface->fbo.depth_rbhandle);
			surface->fbo.stencil_rbhandle = 0;
			surface->fbo.depth_rbhandle = 0;
			glDeleteFramebuffersEXT(1, &surface->fbo.handle);
			surface->fbo.handle = 0;
			CHECKGL;

		xcase SURF_FAKE_PBUFFER:
			// nothing to do

		xcase SURF_PBUFFER:
			if (surface->pbuffer.glRC)
			{
				wglDeleteContext(surface->pbuffer.glRC);
				wglReleasePbufferDCARB(surface->pbuffer.handle, surface->pbuffer.hDC);
				wglDestroyPbufferARB(surface->pbuffer.handle);
			}

		xdefault:
			assertmsg(0, "Unknown surface type!");
	}

	if (surface->depth_handle)
		rwglFreeTextureDirect(device, surface->depth_handle);

	if (surface->tex_handle)
		rwglFreeTextureDirect(device, surface->tex_handle);

	for (i=0; i<3; i++) {
		if (surface->aux_handles[i]) {
			rwglFreeTextureDirect(device, surface->aux_handles[i]);
		}
	}

	surface->state_inited = 0;
}

void rwglFreeSurfaceDirect(RdrDeviceWinGL *device, RdrSurfaceWinGL *surface)
{
	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	rwglFreeSurfaceDataDirect(device, surface);

	rdrRemoveSurfaceTexHandle((RdrSurface *)surface);

	ZeroStruct(surface);

	free(surface);
}


//////////////////////////////////////////////////////////////////////////

__forceinline static void rwglBindSurfaceAuxDirect(RdrSurfaceWinGL *surface, int tex_unit, int buffer_num)
{
	int ret, buffer;
	TexHandle handle=0;

	if (!surface->tex_handle)
	{
		assert(!surface->is_faked_RTT); // If not, this should have already been generated
		surface->tex_handle = rdrGenTexHandle();
	}

	handle = surface->tex_handle;
	if (buffer_num == 0)
	{
		buffer = WGL_FRONT_LEFT_ARB;
	}
	else
	{
		if (!surface->aux_handles[buffer_num-1]) {
			assert(!surface->is_faked_RTT); // If not, this should have already been generated
			surface->aux_handles[buffer_num-1] = rdrGenTexHandle();
		}
		handle = surface->aux_handles[buffer_num-1];
		buffer = WGL_AUX0_ARB + buffer_num - 1;
	}

	assert(handle);
	rwglBindTexture(texTarget, tex_unit, handle);
	glTexParameteri(texTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(texTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (surface->point_sample[buffer_num])
	{
		glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}
	else
	{
		glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}

	if (!surface->is_faked_RTT && surface->type != SURF_FBO)
	{
		GLint texFormat = WGL_NO_TEXTURE_ARB;
		wglQueryPbufferARB(surface->pbuffer.handle, WGL_TEXTURE_FORMAT_ARB, &texFormat);

		if (texFormat == WGL_NO_TEXTURE_ARB)
			assert(0);

		ret = wglBindTexImageARB(surface->pbuffer.handle, buffer);
		assert(ret);
	}
}

__forceinline static void rwglReleaseSurfaceAuxDirect(RdrSurfaceWinGL *surface, int tex_unit, int buffer_num)
{
	int ret, buffer;
	TexHandle handle;

	handle = surface->tex_handle;
	if (buffer_num == 0)
	{
		buffer = WGL_FRONT_LEFT_ARB;
	}
	else
	{
		handle = surface->aux_handles[buffer_num-1];
		buffer = WGL_AUX0_ARB + buffer_num - 1;
	}

	assert(handle);
	rwglBindTexture(texTarget, tex_unit, handle); // Just to set glActiveTextureARB(GL_TEXTURE0_ARB+tex_unit);
	
	if (!surface->is_faked_RTT && surface->type != SURF_FBO)
	{
		ret = wglReleaseTexImageARB(surface->pbuffer.handle, buffer);
		assert(ret);
	}
}

void rwglBindSurfaceDirect(RdrSurfaceWinGL *surface, int tex_unit, RdrSurfaceBuffer buffer)
{
	CHECKDEVICELOCK(surface->surface_base.device);
	CHECKSURFACENOTACTIVE(surface);

	if (buffer == SBUF_DEPTH)
	{
		//assert(surface->creation_flags & SF_DEPTH_TEXTURE);
		if (surface->depth_handle)
		{
			rwglBindTexture(texTarget, tex_unit, surface->depth_handle);
			glTexParameteri(texTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(texTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		}
		else
		{
			rwglBindWhiteTexture(tex_unit);
		}
	}
	else
	{
		rwglBindSurfaceAuxDirect(surface, tex_unit, buffer - SBUF_0);
	}

	surface->is_bound = true;
}

void rwglReleaseSurfaceDirect(RdrSurfaceWinGL *surface, int tex_unit, RdrSurfaceBuffer buffer)
{
	CHECKDEVICELOCK(surface->surface_base.device);
	CHECKSURFACENOTACTIVE(surface);

	if (buffer == SBUF_DEPTH)
	{
		//assert(surface->creation_flags & SF_DEPTH_TEXTURE);
		rwglBindWhiteTexture(tex_unit);
	}
	else
	{
		rwglReleaseSurfaceAuxDirect(surface, tex_unit, buffer - SBUF_0);
	}

	surface->is_bound = false;
}

//////////////////////////////////////////////////////////////////////////

void rwglGetSurfaceDataDirect(RdrSurfaceWinGL *surface, RdrSurfaceData *params)
{
	CHECKGLTHREAD;
	CHECKDEVICELOCK(surface->surface_base.device);
	CHECKSURFACEACTIVE(surface);

	PERFINFO_AUTO_START("rwglGetSurfaceDataDirect", 1);

	PERFINFO_AUTO_START("glReadPixels", 1);
	switch (params->type)
	{
		xcase SURFDATA_RGB:
			glReadPixels(params->x,params->y,params->width,params->height,GL_RGB,GL_UNSIGNED_BYTE,params->data);
		xcase SURFDATA_RGBA:
			glReadPixels(params->x,params->y,params->width,params->height,GL_RGBA,GL_UNSIGNED_BYTE,params->data);
		xcase SURFDATA_DEPTH:
			glReadPixels(params->x,params->y,params->width,params->height,GL_DEPTH_COMPONENT,GL_FLOAT,params->data);
		xcase SURFDATA_STENCIL:
			glReadPixels(params->x,params->y,params->width,params->height,GL_STENCIL_INDEX,GL_UNSIGNED_INT,params->data);
		default:
			devassert(0);
	}
	PERFINFO_AUTO_STOP_START("DownSample", 1);
	if (params->type == SURFDATA_RGBA)
	{
		if (surface->software_multisample_level > 1)
		{
			int w, h, neww, newh;
			int multisample = surface->software_multisample_level;
			assert(params->x == 0 && params->y == 0 && params->width == surface->width && params->height == surface->height);
			w = surface->width;
			h = surface->height;
			while (multisample>1)
			{
				// Downsample in-place
				int	i,x,y,y_in[2];
				U32	pixel,rgba[4],*pix;

				neww = w >> 1;
				newh = h >> 1;

				for (pix = (U32*)params->data, y = 0; y < newh; y++)
				{
					y_in[0] = (y*2+0) * w;
					y_in[1] = (y*2+1) * w;
					for (x = 0; x < neww; x++)
					{
						memset(rgba, 0, sizeof(rgba));
						for (i = 0; i < 4; i++)
						{
							pixel = ((U32*)params->data)[y_in[i>>1] + (x<<1) + (i&1)];
							rgba[0] += (pixel >> 0) & 0xff;
							rgba[1] += (pixel >> 8) & 0xff;
							rgba[2] += (pixel >> 16) & 0xff;
							rgba[3] += (pixel >> 24) & 0xff;
						}
						*pix++ =  (rgba[0] >> 2) << 0
							| (rgba[1] >> 2) << 8
							| (rgba[2] >> 2) << 16
							| (rgba[3] >> 2) << 24;
					}
				}

				w = neww;
				h = newh;
				multisample >>= 1;
			}
		}
		if (surface->type == SURF_FAKE_PBUFFER)
		{
			// We aren't really using a PBuffer here, so try to fake Alpha
			Color	*pix;
			int i, size = surface->width * surface->height;
			for (pix = (Color*)params->data, i=0; i < size; i++, pix++)
			{
				if (pix->r + pix->g + pix->b == 0)
					pix->a=0;
			}
		}
	}
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}
