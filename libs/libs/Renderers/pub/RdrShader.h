#ifndef RDRSHADER_H
#define RDRSHADER_H
GCC_SYSTEM

#include "RdrDevice.h"

typedef int ShaderHandle;
typedef struct FileEntry FileEntry;
typedef FileEntry** FileList;
typedef struct RdrShaderFinishedData RdrShaderFinishedData;
typedef struct GfxShaderFinishedData GfxShaderFinishedData;
typedef struct StashTableImp* StashTable;

#define PROGRAMDEF_DEFINES_NUM 23

typedef struct RxbxProgramDef 
{
	char *filename;
	char *entry_funcname;
	const char * defines[PROGRAMDEF_DEFINES_NUM];
	bool dx11_only;
	bool skip_me;
	bool is_nullshader;
	bool is_minimal;
} RxbxProgramDef;

typedef enum ShaderProgramType {
	SPT_VERTEX,
	SPT_FRAGMENT,
} ShaderProgramType;

typedef void (*RdrShaderCallbackFunc)(RdrShaderFinishedData *finishedData);

typedef struct RdrShaderFinishedData
{
	RdrShaderCallbackFunc finishedCallback;
	GfxShaderFinishedData *userData;

	void *shader_data; // Must be freed by client
	int shader_data_size;
	void *updb_data; // Must be freed by client
	int updb_data_size;
	char *updb_filename; // Must be freed by client
	FileList file_list;

	bool compilationFailed;
} RdrShaderFinishedData;

typedef struct RdrShaderParams {
	ShaderHandle shader_handle;
	ShaderProgramType shader_type;
	const char *shader_debug_name; // for writeProcessedShaders
	const char *shader_error_filename; // If compilation fails
	const char *intrinsic_defines; // To override device settings
	const char *override_shader_model; // To override device settings
	RdrShaderFinishedData *finishedCallbackData; // finishedCallback called in main thread when compilation finishes or fails
	int shader_data_size;
	int num_defines;
	U32 isPrecompiled:1;
	U32 noBackgroundCompile:1; // For post-processing shaders, etc
	U32 hideCompileErrors:1; // For MaterialEditor testing shader models
} RdrShaderParams;

typedef struct RdrShaderPerformanceValues {
	ShaderHandle shader_handle;
	ShaderProgramType shader_type;
	int instruction_count; // <0 indicates compile failure
	int texture_fetch_count;
	int temporaries_count;
	U32 pixel_count;
	U64 nvps_pps; // If rdr_state.runNvPerfShader, gives the PPS on a NV43-GT card
	int dynamic_constant_count; // Set client-side (GraphicsLib), not by RenderLib
	int d3d_instruction_slots; // The instruction slots used up by D3D
	int d3d_alu_instruction_slots; // The ALU instruction slots used up by D3D
} RdrShaderPerformanceValues;

RdrShaderParams *rdrStartUpdateShader(RdrDevice *device, ShaderHandle shader_handle, ShaderProgramType shader_type, const char **defines, int num_defines, const char *shader_data);
RdrShaderParams *rdrStartUpdateShaderPrecompiled(RdrDevice *device, ShaderHandle shader_handle, ShaderProgramType shader_type, const void *shader_data, int shader_data_size);
__forceinline static void rdrEndUpdateShader(RdrDevice *device) { wtSendCmd(device->worker_thread); }
__forceinline static void rdrFreeAllShaders(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_FREEALLSHADERS, 0, 0); }

void rdrShaderLibInit(void);
void rdrShaderLibLoadDLLs(void);
void rdrShaderLibShutdown(RdrDevice *rdr_device);

int rdrShaderGetBackgroundShaderCompileCount(void);
extern volatile int shader_background_compile_count;

void rdrShaderPreloadLog(FORMAT_STR const char *fmt, ...);
#define rdrShaderPreloadLog(fmt, ...) rdrShaderPreloadLog(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
bool rdrShaderPreloadSkip(const char *filename);
bool rdrShaderPreloadSkipEvenCompiling(const char *filename);

void rdrShaderSetGlobalDefine(int index, const char *str);
int rdrShaderGetGlobalDefineCount(void); // Actual results may include NULLs
const char *rdrShaderGetGlobalDefine(int index);

void rdrShaderSetTestDefine(int index, const char *str);
int rdrShaderGetTestDefineCount(void); // Actual results may include NULLs
const char *rdrShaderGetTestDefine(int index);

void rdrShaderGetDebugNameAndHeader(char *debug_fn, int debug_fn_size, char *debug_header, int debug_header_size,
									const char *dir, const char *commentmarker,
									const char *filename, const char *ext,
									bool bIncludeDefines);

extern StashTable g_all_shader_defines;

#endif
