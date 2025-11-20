#ifndef _RT_STATE_H_
#define _RT_STATE_H_

#include "stdtypes.h"
#include "ogl.h"
#include "RdrSurface.h"

typedef int TexHandle;
typedef struct RdrSurfaceWinGL RdrSurfaceWinGL;

typedef enum
{
	GLC_VERTEX_ARRAY			= 1<<0,
	GLC_COLOR_ARRAY				= 1<<1,
	GLC_NORMAL_ARRAY			= 1<<2,
	GLC_TANGENT_ARRAY			= 1<<3,
	GLC_BINORMAL_ARRAY			= 1<<4,
	GLC_BONE_IDXS_ARRAY			= 1<<5,
	GLC_BONE_WEIGHTS_ARRAY		= 1<<6,
	GLC_TEXTURE_COORD_ARRAY_0	= 1<<7,
	GLC_TEXTURE_COORD_ARRAY_1	= 1<<8,
	GLC_TEXTURE_COORD_ARRAY_2	= 1<<9,
	GLC_TEXTURE_COORD_ARRAY_3	= 1<<10,
	GLC_TEXTURE_COORD_ARRAY_4	= 1<<11,
	GLC_TEXTURE_COORD_ARRAY_5	= 1<<12,
	GLC_TEXTURE_COORD_ARRAY_6	= 1<<13,
	GLC_TEXTURE_COORD_ARRAY_7	= 1<<14,
	GLC_MAXBIT					= 1<<15,
} GLClientStateBits;

#define MAX_TEXTURE_UNITS_TOTAL	16
#define MAX_TEXTURE_COORDS 8
#define MAX_PROGRAM_LOCALPARAMS 64
#define MAX_PROGRAM_ENVPARAMS 16

typedef enum ProgramType
{
	GLC_FRAGMENT_PROG = 0,
	GLC_VERTEX_PROG = 1,
} ProgramType;

typedef enum BlendOp
{
	BLENDOP_ADD,
	BLENDOP_SUBTRACT,
} BlendOp;

typedef struct RdrStateWinGL RdrStateWinGL;


//////////////////////////////////////////////////////////////////////////


void rwglSetStateActive(RdrStateWinGL *state, int firsttime, int surface_width, int surface_height);
void rwglResetState(void);
void rwglResetTextureState(void);
RdrStateWinGL *rwglGetCurrentState(void);


void checkgl(char *fname,int line);


// matrices
void rwglSet3DProjection(RdrStateWinGL *state, const Mat44 projection);
void rwglSetViewMatrix(RdrStateWinGL *state, const Mat4 view_mat, const Mat4 inv_view_mat);
void rwglSet3DMode(void);
void rwglSet2DMode(int width, int height);
void rwglPushModelMatrix(const Mat4 model_mat);
void rwglPopModelMatrix(void);
void rwglSetPPTextureSize(int width, int height, int vwidth, int vheight);


// stencil
void rwglEnableStencilTest(int enable);
void rwglStencilFunc(GLenum func, GLint ref, GLuint mask);
void rwglStencilOp(GLenum fail, GLenum zfail, GLenum zpass);


// depth mask
void rwglDepthMask(GLboolean mask);


// vbos
void rwglBindVBO(GLenum target, U32 buffer_id);
void rwglUpdateVBOData(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage, const char *memname);
void rwglDeleteVBO(GLenum target, U32 buffer_id, const char *memname);


// buffer pointers
void rwglEnableArrays(GLClientStateBits enable);
void rwglVertexPointer(GLint size, GLsizei stride, const GLvoid *pointer);					// (assumed F32 type)
void rwglColorPointer(GLint size, GLsizei stride, const GLvoid *pointer);					// (assumed F32 type)
void rwglNormalPointer(GLsizei stride, const GLvoid *pointer);								// (assumed F32 type)
void rwglTangentPointer(GLsizei stride, const GLvoid *pointer);								// (assumed F32 type)
void rwglBinormalPointer(GLsizei stride, const GLvoid *pointer);							// (assumed F32 type)
void rwglBoneIdxPointer(GLint size, GLsizei stride, const GLvoid *pointer);					// (assumed U16 type)
void rwglBoneWeightPointer(GLint size, GLsizei stride, const GLvoid *pointer);				// (assumed F32 type)
void rwglTexCoordPointer(U32 tex_unit, GLint size, GLsizei stride, const GLvoid *pointer);	// (assumed F32 type)
void rwglTexCoord(U32 tex_unit, Vec2 tex_coord);
void rwglVertex(Vec3 v);
void rwglColor(Color color);
void rwglColorf(Vec4 color);
void rwglModelColorf(int index, Vec4 color);


// textures
void rwglBindWhiteTexture(U32 tex_unit);
void rwglBindTexture(GLenum target, U32 tex_unit, TexHandle tex);
void rwglTexLODBias(U32 tex_unit, F32 newbias);


// fog
void rwglFog(int on);
void rwglFogPush(int on);
void rwglFogPop(void);
void rwglFogRange(RdrStateWinGL *state, F32 near_dist, F32 far_dist);
void rwglFogColor(RdrStateWinGL *state, Vec4 clr);


// vertex/fragment programs
void rwglEnableProgram(ProgramType target, int enable);
void rwglBindProgram(ProgramType target, GLuint id);
void rwglProgramLocalParameter(ProgramType target, GLuint n, const GLfloat *vec4);
void rwglProgramEnvParameter(ProgramType target, GLuint n, const GLfloat *vec4);


// blend function
void rwglBlendFunc(GLenum sfactor, GLenum dfactor, BlendOp op);
void rwglBlendFuncPush(GLenum sfactor, GLenum dfactor, BlendOp op);
void rwglBlendFuncPushNop(void);
void rwglBlendFuncPop(void);
void rwglBlendStackFreeze(int freeze);


// lighting
void rwglSetupAmbientLight(const Vec3 ambient);


//////////////////////////////////////////////////////////////////////////


#if 1
#include "../RdrDevicePrivate.h"
#include "file.h"
#define DOING_CHECKGL
#define CHECKGL assert(isDevelopmentMode()); checkgl(__FILE__,__LINE__)
#define CHECKGLTHREAD assert(isDevelopmentMode()); rdrCheckThread(); checkgl(__FILE__,__LINE__)
#define CHECKNOTTHREAD assert(isDevelopmentMode()); rdrCheckNotThread()
#define CHECKDEVICELOCK(device) assert(isDevelopmentMode()); assert(((RdrDevice *)device)->is_locked_thread)
#define CHECKSURFACEACTIVE(surface) assert(isDevelopmentMode()); assert(((RdrDeviceWinGL *)((RdrSurface *)surface)->device)->active_surface == surface)
#define CHECKSURFACENOTACTIVE(surface) assert(isDevelopmentMode()); assert(((RdrDeviceWinGL *)((RdrSurface *)surface)->device)->active_surface != surface)
#else
#define CHECKGL
#define CHECKGLTHREAD
#define CHECKNOTTHREAD
#define CHECKDEVICELOCK(device)
#define CHECKSURFACEACTIVE(surface)
#define CHECKSURFACENOTACTIVE(surface)
#endif


typedef struct RdrStateWinGL
{
	Mat44	projection_mat3d;
	Mat44	inv_projection_mat3d;
	Mat4	viewmat;
	Mat4	inv_viewmat;
	Mat4	modelmat;

	int width_2d, height_2d;


	TexHandle	white_tex_handle;

	struct
	{
		GLenum func;
		GLint  ref;
		GLuint mask;
		GLenum fail;
		GLenum zfail;
		GLenum zpass;
		int    enabled;
	} stencil;

	struct
	{
		GLuint bound_id;
		GLenum bound_target;
		RdrSurfaceWinGL *bound_surface;
		RdrSurfaceBuffer bound_surface_buffer;
		int lod_bias;
	} tex[MAX_TEXTURE_UNITS_TOTAL];

	struct 
	{
		int enabled;
		GLuint bound_program;
		int local_parameters_set[MAX_PROGRAM_LOCALPARAMS];
		Vec4 local_parameters[MAX_PROGRAM_LOCALPARAMS];
		int env_parameters_set[MAX_PROGRAM_ENVPARAMS];
		Vec4 env_parameters[MAX_PROGRAM_ENVPARAMS];
	} program[2];

	struct 
	{
		struct
		{
			GLenum sfactor, dfactor;
			BlendOp op;
		} stack[32];
		int stack_idx;
		int stack_frozen;
	} blend_func;

	struct
	{
		int on;
		int force_off;
		F32 current[2], old[2];
		int stack[4];
		int stack_depth;
		int doneonce;
		Vec4 color;
	} fog;

	struct 
	{
		int last_element_array_id;
		int last_vertex_array_id;
		const GLvoid *verts;
		const GLvoid *colors;
		const GLvoid *norms;
		const GLvoid *tangents;
		const GLvoid *binorms;
		const GLvoid *boneidxs;
		const GLvoid *boneweights;
		const GLvoid *texcoords[MAX_TEXTURE_COORDS];
		int disable;
		int used;
	} vbo;

	U32 clientstatebits;

	GLboolean depthmask;

	Vec3 ambient_light;

} RdrStateWinGL;



#endif //_RT_STATE_H_

