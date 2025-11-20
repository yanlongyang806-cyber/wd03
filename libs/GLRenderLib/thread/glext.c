#define GLH_EXT_SINGLE_FILE
#include "file.h"

#include "ogl.h"
#include "rt_state.h"
#include "../RdrDevicePrivate.h"
#include "RenderLib.h"
#include "systemspecs.h"

#pragma comment(lib, "opengl32.lib")

int gl_clamp_val = GL_CLAMP_TO_EDGE;
RenderCaps rdr_caps;

PFNGLDRAWBUFFERSARBPROC glDrawBuffersARB;


#define INITFUNCPTR(funcname) funcname = (void*)GLH_EXT_GET_PROC_ADDRESS(#funcname)

static int extInit(char *extension,int fatal)
{
	int glt;

	CHECKGLTHREAD;
	glt = glh_init_extensions(extension);
	if (!glt)
	{
		if (fatal)
		{
			char buf[256];
			sprintf_s(SAFESTR(buf), "Your card or driver doesn't support %s",extension);
			rdrAlertMsg(0, buf);
		}
		return 0;
	}
	return 1;
}

int rwglInitExtensions(void)
{

	// it is not technically accurate to have global function pointers,
	// since they will point into different drivers if you have
	// multiple graphics cards, but that should never happen.
	if (rdr_caps.filled_in)
		return 1;

	INITFUNCPTR(wglCreatePbufferARB);
	INITFUNCPTR(wglGetPbufferDCARB);
	INITFUNCPTR(wglReleasePbufferDCARB);
	INITFUNCPTR(wglDestroyPbufferARB);
	INITFUNCPTR(wglQueryPbufferARB);
	INITFUNCPTR(wglChoosePixelFormatARB);
	INITFUNCPTR(wglBindTexImageARB);
	INITFUNCPTR(wglReleaseTexImageARB);
	INITFUNCPTR(wglGetPixelFormatAttribivARB);
	INITFUNCPTR(wglGetPixelFormatAttribfvARB);
	INITFUNCPTR(wglSwapIntervalEXT);
	INITFUNCPTR(glDrawBuffersARB);


	if (!extInit("GL_ARB_multitexture",1))
		return 0;
	if (!extInit("GL_ARB_texture_compression",1))
		return 0;
	if (!extInit("GL_ARB_vertex_program", 1))
		return 0;
	if (!extInit("GL_ARB_fragment_program", 1))
		return 0;
	if (!extInit("WGL_ARB_pbuffer", 1))
		return 0;
	if (!extInit("GL_EXT_framebuffer_object", 1))
		return 0;

	rdr_caps.supports_vbos = extInit("GL_ARB_vertex_buffer_object", 0);
	rdr_caps.supports_arb_np2 = extInit("GL_ARB_texture_non_power_of_two", 0);
	rdr_caps.supports_pixel_format_float = extInit("WGL_ATI_pixel_format_float", 0);
	rdr_caps.supports_render_texture = extInit("WGL_ARB_render_texture", 0);
	rdr_caps.supports_multisample = extInit("WGL_ARB_multisample", 0);
	rdr_caps.supports_anisotropy = extInit("GL_EXT_texture_filter_anisotropic", 0);
	rdr_caps.supports_subtract = extInit("GL_EXT_blend_minmax", 0) && extInit("GL_EXT_blend_subtract", 0);
	rdr_caps.supports_shadowmaps = extInit("GL_ARB_fragment_program_shadow",0);

	systemSpecsVideoCardIdentification(rdr_caps.videoCardName, sizeof(rdr_caps.videoCardName), &rdr_caps.videoCardVendorID, &rdr_caps.videoCardDeviceID);

	if (rdr_caps.videoCardVendorID == VENDOR_ATI) {
		rdr_caps.copy_subimage_from_rtt_invert_hack = 1;
		if (system_specs.videoDriverVersionNumber < 6000)
			rdr_caps.copy_subimage_shifted_from_fbos_hack = 1;
	}

	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &rdr_caps.maxTexUnits);
	glGetIntegerv(GL_MAX_TEXTURE_COORDS_ARB, &rdr_caps.maxTexCoords);

	if (rdr_caps.supports_anisotropy)
	{
		float largest_supported_anisotropy;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest_supported_anisotropy);
		rdr_caps.maxTexAnisotropic = largest_supported_anisotropy;
	}
	else
	{
		rdr_caps.maxTexAnisotropic = 1;
	}

	rdr_caps.filled_in = 1;

	return 1;
}


