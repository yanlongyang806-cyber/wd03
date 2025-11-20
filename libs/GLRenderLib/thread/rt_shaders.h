#ifndef _RT_SHADERS_H_
#define _RT_SHADERS_H_

#include "rt_state.h"

typedef struct RdrDeviceWinGL RdrDeviceWinGL;
typedef struct RdrShaderParams RdrShaderParams;
typedef struct RdrShaderPerformanceValues RdrShaderPerformanceValues;

typedef char *STR;

typedef struct RwglProgramDef 
{
	char *filename;
	STR defines[10];
} RwglProgramDef;


void rwglLoadPrograms(RdrDeviceWinGL *device, ProgramType type, RwglProgramDef *defs, ShaderHandle *progs, int count, int firsttime);
void rwglSetShaderDataDirect(RdrDeviceWinGL *device, RdrShaderParams *params);
void rwglQueryShaderPerfDirect(RdrDeviceWinGL *device, RdrShaderPerformanceValues *params);

#endif //_RT_SHADERS_H_
