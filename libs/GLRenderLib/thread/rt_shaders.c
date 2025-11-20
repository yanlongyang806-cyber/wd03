#include "utils.h"
#include "StashTable.h"
#include "error.h"
#include "file.h"
#include "EString.h"
#include "mathutil.h"
#include "timing.h"
#include "crypt.h"

#include "ogl.h"
#include "rt_shaders.h"
#include "rt_state.h"
#include "device.h"
#include "RenderLib.h"
#include "../RdrShaderPrivate.h"
#include "systemspecs.h"

static bool loadProgram(RdrDeviceWinGL *device, const char* filename, GLenum target, ShaderHandle handle);

static void addVendorDefines(void)
{
	if (rdr_caps.videoCardVendorID == VENDOR_ATI)
		rdrShaderAddDefine("ATI");
	else if (rdr_caps.videoCardVendorID == VENDOR_NV)
		rdrShaderAddDefine("NV");
	else if (rdr_caps.videoCardVendorID == VENDOR_INTEL)
		rdrShaderAddDefine("INTEL");
	else if (rdr_caps.videoCardVendorID == VENDOR_S3G)
		rdrShaderAddDefine("S3G");
	else if (rdr_caps.videoCardVendorID == VENDOR_WINE)
		rdrShaderAddDefine("WINE");
}

void rwglLoadPrograms(RdrDeviceWinGL *device, ProgramType type, RwglProgramDef *defs, ShaderHandle *progs, int count, int firsttime)
{
	int i, j;
	GLenum gltype = (type == GLC_VERTEX_PROG) ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB;

	PERFINFO_AUTO_START("rwglLoadPrograms", 1);

	rdrShaderEnterCriticalSection();
	rdrShaderResetCache(true);

	PERFINFO_AUTO_START("GenPrograms", 1);

	rwglEnableProgram(type, 0);

	if (firsttime)
	{
		// Generate the program objects
		for (i = 0; i < count; i++)
			progs[i] = rdrGenShaderHandle();
	}

	PERFINFO_AUTO_STOP_START("LoadPrograms", 1);

	// Load the vertex programs
	for (i = 0; i < count; i++)
	{
		for (j = 0; defs[i].defines[j] && defs[i].defines[j][0]; j++)
			rdrShaderAddDefine(defs[i].defines[j]);
		addVendorDefines();
		loadProgram(device, defs[i].filename, gltype, progs[i]);
	}

	PERFINFO_AUTO_STOP();

	rdrShaderResetCache(false);
	rdrShaderLeaveCriticalSection();

	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////

static GLenum shaderSendToGL(RdrDeviceWinGL *device, GLenum target, ShaderHandle handle, const char *programText)
{
	GLenum result = GL_NO_ERROR;
	PERFINFO_AUTO_START("gl", 1);
		CHECKGL;
		// Bind the program object
		glBindProgramARB(target, handle);
		CHECKGL;

		// Compile the program
		glProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(programText), programText);
		// Check to see if the program compiled
		result = glGetError();

		if(result != GL_NO_ERROR) {
			// Sometimes it fails the first time on ATI?
			// Compile the program
			glProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(programText), programText);
			// Check to see if the program compiled
			result = glGetError();
		}
/*		if (perfThisShader) {
			int i, iv;
			struct {
				int glid;
				char *name;
			} fields[] = {
				{GL_PROGRAM_INSTRUCTIONS_ARB, "Ins"},
				{GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB, "NatIns"},
				{GL_PROGRAM_TEMPORARIES_ARB, "ProgTemp"},
				{GL_PROGRAM_NATIVE_TEMPORARIES_ARB, "NatTemp"},
				{GL_PROGRAM_ALU_INSTRUCTIONS_ARB, "ALU"},
				{GL_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB, "NatALU"},
				{GL_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB, "NatTEX"},
			};
			if (!(target == GL_FRAGMENT_PROGRAM_ARB)) {
				OutputDebugStringf("%s\r\n", debug_fn);
			}
			OutputDebugStringf("     ");
			for (i=0; i<ARRAY_SIZE(fields); i++) {
				glGetProgramivARB(target, fields[i].glid, &iv);
				OutputDebugStringf("%s:%d ", fields[i].name, iv);
			}
			OutputDebugStringf("\r\n");

		}
*/
	PERFINFO_AUTO_STOP();
	// Check for errors
	if (result != GL_NO_ERROR) {
		const char *errstring;
		if (result == GL_INVALID_ENUM && target == GL_FRAGMENT_PROGRAM_ARB) {
			// GL_FRAGMENT_PROGRAM_ARB is not supported
			return result;
		}

		OutputDebugStringf("Shader Compilation failure\r\n");
		OutputDebugStringf("Defines: %s\r\n", rdrGetDefinesString());
		errstring = glGetString(GL_PROGRAM_ERROR_STRING_ARB);
		if (errstring) {
			int line=0, column=0;
			OutputDebugStringf("%s", errstring);
			sscanf(errstring, "line %d, column %d", &line, &column);
			if (line) {
				const char *ptr=programText;
				line--;
				while (line && ptr) {
					line--;
					ptr = strchr(ptr, '\n');
					if (ptr)
						ptr++;
				}
				if (ptr) {
					char buf[1024];
					const char *eol = strchr(ptr, '\n');
					if (!eol)
						eol = ptr + strlen(ptr);
					if (eol==ptr) {
						buf[0]=0;
					} else {
						strncpyt(buf, ptr, MIN(eol - ptr, ARRAY_SIZE(buf)));
					}
					OutputDebugStringf("%s\r\n", buf);
					for (column; column>=0; column--)
						OutputDebugString(" ");
					OutputDebugString(" ^");
				}
			}
		}
		OutputDebugStringf("\r\n");
		rdrStatusPrintf("Shader compilation FAILED");
	}

	return result;
}

static int lpcGetKey(GLenum target, ShaderHandle handle)
{
	int key = 0;
	switch(target)
	{
		xcase GL_FRAGMENT_PROGRAM_ARB:
			key = 0x10000000;
		xcase GL_VERTEX_PROGRAM_ARB:
			key = 0x20000000;
		xdefault:
			assert(0);
	}

	key |= handle;

	return key;
}

static U32 lpcGetCachedCrc(RdrDeviceWinGL *device, GLenum target, ShaderHandle handle)
{
	int key = lpcGetKey(target, handle);
	U32 cached_crc;

	if (!device->lpc_crctable)
		device->lpc_crctable = stashTableCreateInt(64);

	if (stashIntFindInt(device->lpc_crctable, key, &cached_crc))
		return cached_crc;
	return 0;
}

static void lpcUpdateCachedCrc(RdrDeviceWinGL *device, GLenum target, ShaderHandle handle, U32 crc)
{
	int key = lpcGetKey(target, handle);

	if (!device->lpc_crctable)
		device->lpc_crctable = stashTableCreateInt(64);

	stashIntAddInt(device->lpc_crctable, key, crc, true);
}

// This loads a program and compiles it, replaces ATI/NV/FP specific lines where necessary
// returns true on success
static bool loadProgram(RdrDeviceWinGL *device, const char* filename, GLenum target, ShaderHandle handle)
{
	U32 cached_crc, new_crc=0;
	char*  programText;
	GLenum result = GL_NO_ERROR;
	PERFINFO_AUTO_START("loadProgram", 1);

	assert((target == GL_VERTEX_PROGRAM_ARB) || (target == GL_FRAGMENT_PROGRAM_ARB));
	assert(handle > 0);

	CHECKGL;

	cached_crc = lpcGetCachedCrc(device, target, handle);
	programText = rdrLoadShaderData(filename, "shaders/wingl", "#", cached_crc, &new_crc, NULL);
	if (programText)
	{
		if (cached_crc != new_crc)
		{
			result = shaderSendToGL(device, target, handle, programText);
			lpcUpdateCachedCrc(device, target, handle, new_crc);
		}
	}

	if(result != GL_NO_ERROR)
	{
		PERFINFO_AUTO_START("Error", 1);
		if (result == GL_INVALID_ENUM && target == GL_FRAGMENT_PROGRAM_ARB) {
			// GL_FRAGMENT_PROGRAM_ARB is not supported
			rdrFreeShaderData();
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return false;
		}

		if (strStartsWith(filename, "shaders/wingl/error.")) {
			// Don't recursively try to load error.*
			rdrFreeShaderData();
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return false;
		}

		if(target == GL_FRAGMENT_PROGRAM_ARB)
		{
			loadProgram(device, "shaders/wingl/error.fp", GL_FRAGMENT_PROGRAM_ARB, handle);
		}
		else if(target == GL_VERTEX_PROGRAM_ARB)
		{
			loadProgram(device, "shaders/wingl/error.vp", GL_VERTEX_PROGRAM_ARB, handle);
		}
		PERFINFO_AUTO_STOP();
	}

	// Clean up
	rdrFreeShaderData();
	PERFINFO_AUTO_STOP();
	return result == GL_NO_ERROR;
}







//////////////////////////////////////////////////////////////////////////
// New shaders stuff

void rwglSetShaderDataDirect(RdrDeviceWinGL *device, RdrShaderParams *params)
{
	int i;
	U32 cached_crc, new_crc = 0;
	GLenum result;
	GLenum target = (params->shader_type==SPT_FRAGMENT)?GL_FRAGMENT_PROGRAM_ARB:GL_VERTEX_PROGRAM_ARB;
	ShaderHandle handle = params->shader_handle;
	int special_defines_size = sizeof(char*)*params->num_defines;
	char **special_defines = (char**)(params+1);
	char *programText = strdup((char*)(params +1) + special_defines_size);

	rdrShaderEnterCriticalSection();
	rdrShaderResetCache(true);

	// Preprocess
	addVendorDefines();
	for (i=0; i<params->num_defines; i++)
		rdrShaderAddDefine(special_defines[i]);
	cached_crc = lpcGetCachedCrc(device, target, handle);
	rdrPreProcessShader(&programText, "shaders/wingl", params->shader_debug_name, ".fp", "#", cached_crc, &new_crc, NULL);
	if (cached_crc != new_crc)
	{
		result = shaderSendToGL(device, target, handle, programText);
		if (result != GL_NO_ERROR) {
			const char *error_filename = params->shader_error_filename;
			if(!error_filename && target == GL_FRAGMENT_PROGRAM_ARB)
			{
				error_filename = "shaders/wingl/error.fp";
			}
			else if(!error_filename && target == GL_VERTEX_PROGRAM_ARB)
			{
				error_filename = "shaders/wingl/error.vp";
			}
			addVendorDefines();
			for (i=0; i<params->num_defines; i++)
				rdrShaderAddDefine(special_defines[i]);
			loadProgram(device, error_filename, target, handle);
		}
		lpcUpdateCachedCrc(device, target, handle, new_crc);
	}
	free(programText);

	rdrShaderResetCache(false);
	rdrShaderLeaveCriticalSection();
}


void rwglQueryShaderPerfDirect(RdrDeviceWinGL *device, RdrShaderPerformanceValues *params)
{
	int i, iv;
	struct {
		int glid;
		char *name;
		int *store;
	} fields[] = {
		{GL_PROGRAM_INSTRUCTIONS_ARB, "Ins", &params->instruction_count},
		{GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB, "NatIns"},
		{GL_PROGRAM_TEMPORARIES_ARB, "ProgTemp", &params->temporaries_count},
		{GL_PROGRAM_NATIVE_TEMPORARIES_ARB, "NatTemp"},
		{GL_PROGRAM_ALU_INSTRUCTIONS_ARB, "ALU"},
		{GL_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB, "NatALU"},
		{GL_PROGRAM_TEX_INSTRUCTIONS_ARB, "TEX", &params->texture_fetch_count},
		{GL_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB, "NatTEX"},
	};
	GLenum target = (params->shader_type==SPT_FRAGMENT)?GL_FRAGMENT_PROGRAM_ARB:GL_VERTEX_PROGRAM_ARB;
	ShaderHandle handle = params->shader_handle;

	glBindProgramARB(target, params->shader_handle);

	//OutputDebugStringf("     ");
	for (i=0; i<ARRAY_SIZE(fields); i++) {
		glGetProgramivARB(target, fields[i].glid, &iv);
		//OutputDebugStringf("%s:%d ", fields[i].name, iv);
		if (fields[i].store) {
			*(fields[i].store) = iv;
		}
	}
	//OutputDebugStringf("\r\n");
}

