#ifndef _xOGL_H
#define _xOGL_H

#include "wininclude.h"
#include <gl/gl.h>
#include <gl/glu.h>

#include "glh_extensions.h"
#include "glh_genext.h"

extern int gl_clamp_val;

int rwglInitExtensions(void);

typedef struct
{
	U32		supports_vbos : 1;
	U32		supports_arb_np2:1;
	U32		supports_pixel_format_float:1;
	U32		supports_render_texture:1;
	U32		supports_subtract:1;
	U32		supports_multisample:1;
	U32		supports_anisotropy:1;
	U32		supports_shadowmaps:1;
	U32		copy_subimage_from_rtt_invert_hack:1;
	U32		copy_subimage_shifted_from_fbos_hack:1;
	U32		filled_in:1; // Whether or not the rdr_caps structure has been filled in yet

	int		maxTexAnisotropic;
	int		maxTexUnits;
	int		maxTexCoords;

	char videoCardName[256];
	int videoCardVendorID;
	int videoCardDeviceID;

} RenderCaps;

extern RenderCaps rdr_caps;


extern PFNGLDRAWBUFFERSARBPROC glDrawBuffersARB;

#endif
