#ifndef _SURFACE_H_
#define _SURFACE_H_

#include "RdrSurface.h"
#include "rt_state.h"

typedef struct RdrDeviceWinGL RdrDeviceWinGL;


typedef enum RwglSurfaceType
{
	SURF_UNINITED = 0,
	SURF_PRIMARY,
	SURF_FAKE_PBUFFER,
	SURF_PBUFFER,
	SURF_FBO,
} RwglSurfaceType;

typedef struct RdrSurfaceWinGL
{
	// RdrSurface MUST be first in struct!
	RdrSurface surface_base;

	// type of surface
	RwglSurfaceType type;

	int width, height;

	RdrStateWinGL state;

	union
	{
		// SURF_PRIMARY
		struct 
		{
			// device and render contexts
			HDC				hDC;
			HGLRC			glRC;
		} primary;

		// SURF_PBUFFER
		struct 
		{
			HPBUFFERARB		handle;
			HDC				hDC;
			HGLRC			glRC;
			int				depth_bits;
			int				depth_format;
		} pbuffer;

		// SURF_FBO
		struct 
		{
			GLuint			handle;
			GLuint			depth_rbhandle;		// handle to depth render buffer
			GLuint			stencil_rbhandle;	// handle to stencil render buffer
		} fbo;
	};

	RdrSurfaceFlags creation_flags;
	RdrSurfaceBufferType buffer_types[4];
	int point_sample[4];
	int tex_formats[4];

	TexHandle	tex_handle; // GL Texture handle if using as render to texture
	TexHandle	depth_handle; // GL Texture handle to depth texture if FBO or rendering depth to texture

	TexHandle	aux_handles[4];		// texture handles for aux buffers
	U32			virtual_width;
	U32			virtual_height;
	U32			hardware_multisample_level;
	U32			software_multisample_level;
	U32			desired_multisample_level;
	U32			required_multisample_level;
	U32			is_bound:1;
	U32			is_faked_RTT:1; // Pretend RTT (copying texture manually)
	U32			need_depth_texture:1;
	U32			state_inited:1;

} RdrSurfaceWinGL;


void rwglInitPrimarySurface(RdrDeviceWinGL *device);
RdrSurface *rwglCreateSurface(RdrDevice *device, RdrSurfaceParams *params);

#endif //_SURFACE_H_
