#include "RdrShader.h"
#include "RenderLib.h"
#include "utils.h"
#include "EString.h"
#include "file.h"
#include "error.h"
#include "StashTable.h"
#include "timing.h"
#include "network/crypt.h"
#include <errno.h>
#include "RdrState.h"
#include "strings_opt.h"
#include "fileutil.h"
#include "StringCache.h"
#include "structInternals.h"
#include "DynamicCache.h"
#include "fileCache.h"
#include "GenericPreProcess.h"
#include "RdrShaderPrivate.h"
#include "ShaderServerInterface.h"
#include "AutoGen/ShaderServerInterface_h_ast.c"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

// May also need to increase SHADER_CACHE_VERSION
#define RDR_SHADER_CACHE_VERSION 9 // Increased for D3DCompiler_42

static struct {
	CRITICAL_SECTION shader_cs;
	char *shader_text;
	bool shader_from_cache;
/*	char commentmarker[2];

	int numMacros;
	struct {
		char *name; // pointer to other data
		MacroParams params;
		char *body; // estring, destroy me!
	} macros[64];

	int numDefines;
	int numSavedDefines;
	char *defines[64];
	char *savedDefines[64];

	char *workingBase;
	char *origBase;
	char *defines_string;*/

	U32 last_defines_crc;

	bool lpc_enabled;
	StashTable lpc_table;

	DynamicCache *precompiled_cache;

	const char **shaderTestDefines;
	const char **shaderGlobalDefines;
} shader_state;

StashTable g_all_shader_defines;

volatile int shader_background_compile_count;

int rdrShaderGetBackgroundShaderCompileCount(void)
{
	return shader_background_compile_count;
}

// Shared by the shader_state.lpc_table and shader file cache cleanup code.
static void freeFunc(char *str)
{
	free(str);
}

static int rdrIsShaderFile(const char * filePath)
{
	return ( !stricmp(filePath, "phl") || !stricmp(filePath, "vhl") ||
		!stricmp(filePath, "hlsl") );
}

static void rdrFindDefinesForTabComplete(const char *file_data)
{
	if (file_data) {
		const char *cursor;
		for (cursor = file_data; *cursor; cursor++) {
			if (strStartsWith(cursor, "#ifdef") || strStartsWith(cursor, "#ifndef")) {
				const char *cursor2 = cursor + 7;
				const char *wordstart = cursor2;
				char word[64];
				while(*wordstart == ' ' || *wordstart=='\t')
					wordstart++;
				cursor2 = wordstart;
				while (*cursor2 && !strchr("\n\r", *cursor2))
				{
					while (*cursor2 && !strchr(" \t\n\r|", *cursor2))
						cursor2++;
					strncpy(word, wordstart, (size_t)(cursor2 - wordstart));
					if (isalpha((unsigned char)word[0])) {
						if (!g_all_shader_defines) {
							g_all_shader_defines = stashTableCreateWithStringKeys(16, StashDefault);
						}
						stashAddInt(g_all_shader_defines, allocAddCaseSensitiveString(word), 1, true);
					}
					wordstart = cursor2;
					while (strchr("\t |", *wordstart))
						wordstart++;
					cursor2 = wordstart;
				}
			}
		}
	}
}

static FileScanAction rdrCacheShaderFileCB(char* dir, struct _finddata32_t* data, void *pUserData)
{
	if ( !(data->attrib & _A_SUBDIR) )
	{
		char *ext;

		ext = strrchr( data->name, '.' );
		if (ext) {
			char *filePath=NULL;
			estrStackCreateSize(&filePath, CRYPTIC_MAX_PATH);
			estrPrintf(&filePath, "%s/%s", dir, data->name);
			ext++;
			if (rdrIsShaderFile( ext ))
			{
				const char *file_data = fileCachUpdateAndGetData(filePath, NULL, data->time_write);
				rdrFindDefinesForTabComplete(file_data);
			} else if (stricmp(ext, "Light")==0 || stricmp(ext, "LightingModel")==0) {
				char *file_data = fileAlloc(filePath, NULL);
				rdrFindDefinesForTabComplete(file_data);
				fileFree(file_data);
			}
			estrDestroy(&filePath);
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}

void rdrShaderCacheFiles( void )
{
#if PLATFORM_CONSOLE
	// Don't cache anything, we shouldn't be loading these at run-time
#else
	if (isDevelopmentMode()) // Production mode should have these precompiled
		fileScanAllDataDirs( "shaders", rdrCacheShaderFileCB, NULL );
#endif
}

RdrShaderParams *rdrStartUpdateShader(RdrDevice *device, ShaderHandle shader_handle, ShaderProgramType shader_type, const char **defines, int num_defines, const char *shader_data)
{
	RdrShaderParams *params;
	int defines_byte_count = num_defines*sizeof(char*);
	int shader_byte_count = (int)strlen(shader_data)+1;

	params = wtAllocCmd(device->worker_thread, RDRCMD_UPDATESHADER, sizeof(*params)+defines_byte_count+shader_byte_count);
	assert( NULL != params );
	ZeroStruct(params);
	params->shader_handle = shader_handle;
	params->shader_type = shader_type;
	params->num_defines = num_defines;
	memcpy(params + 1, defines, defines_byte_count);
	memcpy((U8*)(params + 1) + defines_byte_count, shader_data, shader_byte_count);

	return params;
}

RdrShaderParams *rdrStartUpdateShaderPrecompiled(RdrDevice *device, ShaderHandle shader_handle, ShaderProgramType shader_type, const void *shader_data, int shader_data_size)
{
	RdrShaderParams *params;

	params = wtAllocCmd(device->worker_thread, RDRCMD_UPDATESHADER, sizeof(*params)+shader_data_size);
	assert( NULL != params );
	ZeroStruct(params);
	params->shader_handle = shader_handle;
	params->shader_type = shader_type;
	params->isPrecompiled = 1;
	params->shader_data_size = shader_data_size;
	memcpy(params + 1, shader_data, shader_data_size);

	return params;
}


//////////////////////////////////////////////////////////////////////////

AUTO_RUN_EARLY;
void rdrShaderInitCriticalSection(void)
{
	InitializeCriticalSection(&shader_state.shader_cs);
}

static int rdr_shader_critical_section_depth=0;
void rdrShaderEnterCriticalSection(void)
{
	EnterCriticalSection(&shader_state.shader_cs);
	genericPreProcEnterCriticalSection();
	rdr_shader_critical_section_depth++;
	if (rdr_shader_critical_section_depth==1)
		assert(!shader_state.shader_text); // Previous caller should have freed it
}

void rdrShaderLeaveCriticalSection(void)
{
	rdr_shader_critical_section_depth--;
	if (rdr_shader_critical_section_depth==0)
		assert(!shader_state.shader_text); // Should have called rdrFreeShaderData

	genericPreProcLeaveCriticalSection();
	LeaveCriticalSection(&shader_state.shader_cs);
}

void rdrShaderAssertInCriticalSection(void)
{
	if (!TryEnterCriticalSection(&shader_state.shader_cs))
	{
		assertmsg(0, "Function called while not in the critical section!");
	} else {
		assertmsg(rdr_shader_critical_section_depth!=0, "Function called while not in the critical section!");
		LeaveCriticalSection(&shader_state.shader_cs);
	}
}











void rdrShaderResetCache(bool shouldDoCaching)
{
	// It plays havok with the more lower level shader caching (data from this
	// cache is missing file lists, and this cache is only hit if the other
	// cache misses, which shouldn't happen except while tweaking shaders, so
	// it doesn't greatly matter, but we may want to re-enable this to make debug
	// shader reloading faster if our preprocessor ever starts taking even a fraction
	// of the amount of time D3DCompiler takes to compile a shader).
	shouldDoCaching = false; 

	shader_state.lpc_enabled = shouldDoCaching;
	if (shader_state.lpc_enabled) {
		if (shader_state.lpc_table) {
			stashTableClearEx(shader_state.lpc_table, NULL, freeFunc);
		} else {
			shader_state.lpc_table = stashTableCreateWithStringKeys(16, StashDefault|StashDeepCopyKeys_NeverRelease);
		}
	} else {
		stashTableDestroyEx(shader_state.lpc_table, NULL, freeFunc);
		shader_state.lpc_table = NULL;
	}
}

static char *loadProgramCacheFind(const char *filename) // Returns duplicate copy
{
	const char *orig_string;
	if (!shader_state.lpc_enabled)
		return NULL;
	if (stashFindPointerConst(shader_state.lpc_table, filename, &orig_string)) {
		return strdup(orig_string);
	}
	return NULL;
}
static void loadProgramCacheStore(const char *filename, const char *programText)
{
	if (!shader_state.lpc_enabled)
		return;
	assert(!loadProgramCacheFind(filename));
	stashAddPointer(shader_state.lpc_table, filename, strdup(programText), true);
}

static U32 hashShader(const char *text)
{
	U32 crc=0;

	cryptAdler32Update(text, (int)strlen(text));
	// Hash defines
/*	for (i=0; i<shader_state.numDefines; i++) {
		cryptAdler32Update(shader_state.defines[i], (int)strlen(shader_state.defines[i]));
		}*/
	
	genericPreProcHashDefines_ongoing();
	
	return cryptAdler32Final();
}

bool rdrFindPrecompiledShaderSync_WillStall(SA_PARAM_NN_STR const char *filename, SA_PARAM_NN_STR const char *entrypoint, SA_PRE_NN_FREE SA_POST_NN_VALID void **data, SA_PRE_NN_FREE SA_POST_NN_VALID int *data_size, SA_PARAM_NN_STR const char *device_id, bool bUpdateTimestamp)
{
	// Note: only called within rdrShader CriticalSection
	char fullname[MAX_PATH];
	sprintf(fullname, "%s_%s_%08x", filename, entrypoint, genericPreProcHashDefines());

	if (!rdr_state.disableShaderCache && shader_dynamicCacheIsFileUpToDateSync_WillStall(shader_state.precompiled_cache, fullname)) {
		if (bUpdateTimestamp)
			dynamicCacheTouchFile(shader_state.precompiled_cache, fullname);
		return true;
	}

	return false;
}


bool rdrLoadPrecompiledShaderSync(const char *filename, const char *entrypoint, void **data, int *data_size, const char* device_id)
{
	// Note: only called within rdrShader CriticalSection
	char fullname[MAX_PATH];
	sprintf(fullname, "%s_%s_%08x", filename, entrypoint, genericPreProcHashDefines());

	if (!rdr_state.disableShaderCache && (*data = dynamicCacheGetSync(shader_state.precompiled_cache, fullname, data_size)))
	{
		return true;
	}

	*data = NULL;
	*data_size = 0;
	return false;
}

void rdrSavePrecompiledShader(const char *filename, const char *entrypoint, void *data, int data_size, FileList *file_list, const char* device_id)
{
	if (!rdr_state.disableShaderCache) {
 		// Note: only called within rdrShader CriticalSection
 		char fullname[MAX_PATH];
 		sprintf(fullname, "%s_%s_%08x", filename, entrypoint, shader_state.last_defines_crc);

		dynamicCacheUpdateFile(shader_state.precompiled_cache, fullname, data, data_size, file_list);
	}
}

static void fixCRLF(char *text)
{
	char *out=text, *in=text;
	while (*in) {
		if (*in=='\r') {
			// Skip \rs
			in++;
		} else {
			*out++ = *in++;
		}
	}
	*out='\0';
}

void rdrShaderGetDebugNameAndHeader(char *debug_fn, int debug_fn_size, char *debug_header, int debug_header_size,
								  const char *dir, const char *commentmarker,
								  const char *filename, const char *ext,
								  bool bIncludeDefines)
{
	int i;
	char *s;
	strcpy_s(SAFESTR2(debug_fn), dir);
	strcat_s(SAFESTR2(debug_fn), "/");
	if (strStartsWith(filename, "materials/")) {
		strcat_s(SAFESTR2(debug_fn), filename + strlen("materials/"));
	} else if (strStartsWith(filename, "shaders/")) {
		strcat_s(SAFESTR2(debug_fn), filename + strlen("shaders/"));
	} else {
		strcat_s(SAFESTR2(debug_fn), filename);
	}
	if ((s=strrchr(debug_fn, '.'))) {
		char *therest = strstr(s, "__");
		if (therest)
		{
			strcpy_s(s, debug_fn_size - (s - debug_fn), therest+1);
		} else {
			*s=0;
		}
	}

	if (!bIncludeDefines) {
		strcatf_s(SAFESTR2(debug_fn), "_%08x", genericPreProcHashDefines());
	}

	if (debug_header)
	{
		sprintf_s(SAFESTR2(debug_header), "%s Filename: %s\n", commentmarker, filename);
		strcatf_s(SAFESTR2(debug_header), "%s Defines:\n", commentmarker);
	}

	for (i=0; i < genericPreProcGetNumDefines(); i++) {
		if (bIncludeDefines)
			strcatf_s(SAFESTR2(debug_fn), "_%s", genericPreProcGetNthDefine(i));
		if (debug_header)
			strcatf_s(SAFESTR2(debug_header), "%s   %s\n", commentmarker, genericPreProcGetNthDefine(i));
	}
	strcat_s(SAFESTR2(debug_fn), ext);
	if (debug_header)
		strcat_s(SAFESTR2(debug_header), "\n");

#if _XBOX
	// Max path length on Xbox is 64, and we need room for a drive specifier and .asm, so
	//  trim it down.
	if (strlen(debug_fn) >= 58) {
		static int counter=0;
		char *s = strstri(debug_fn, dir);
		s += strlen(dir) + 1;
		s = strchr(s, '_');
		if (s) {
			*s = '\0';
		} else {
			debug_fn[58 - 14] = '\0';
		}
		strcatf_s(SAFESTR2(debug_fn), ".%d%s", counter, ext);
		counter++;
	}
#endif

}

static void writeDebugShaderData(const char *dir, const char *debug_fn, const char *debug_header, const char *shader_text)
{
	FILE *f;
	char debug_fn_full[MAX_PATH];
	int chmodResult;

	fileLocateWrite(debug_fn, debug_fn_full);
	mkdirtree(debug_fn_full);
	chmodResult = _chmod(debug_fn_full, _S_IREAD | _S_IWRITE);
	if ( chmodResult == -1 )
	{
		if ( errno == ENOENT )
			// errno== ENOENT is OK because the following will create the file
			chmodResult = 0;
	}
	if ( chmodResult != -1 )
	{
		f = fileOpen(debug_fn_full, "w");
		if ( f )
		{
			fwrite(debug_header, 1, strlen(debug_header), f);
			fwrite(shader_text, 1, strlen(shader_text), f);
			fclose(f);
		}
	}
}

// This loads a program and compiles it, replaces ATI/NV/FP specific lines where necessary
// returns true on success
char *rdrLoadShaderData(const char* filename, const char *path, const char *commentmarker, U32 cached_crc, U32 *new_crc, FileList *file_list,
						char *debug_fn, int debug_fn_size, char *debug_header, int debug_header_size)
{
	assert(filename);

	if (file_list)
		FileListInsertChecksum(file_list, filename, 0);

	shader_state.last_defines_crc =  genericPreProcHashDefines();
	shader_state.shader_from_cache = false;
	assert(strlen(commentmarker) <= 2);

	if (rdr_state.stripDeadShaderCode)
		genericPreProcSetCommentMarkers('\0', '\0');
	else
		genericPreProcSetCommentMarkers(commentmarker[0], commentmarker[1]);

	PERFINFO_AUTO_START("rdrLoadShader", 1);

	errorIsDuringDataLoadingInc(filename);

	// Look for already pre-processed version in cache
	PERFINFO_AUTO_START("loadProgramCacheFind",1);
	shader_state.shader_text = loadProgramCacheFind(filename); // Returned duplicate copy
	PERFINFO_AUTO_STOP();

	if (shader_state.shader_text)
	{
		shader_state.shader_from_cache = true;
	}
	else
	{
		shader_state.shader_text = strdup( fileCachedData(filename, NULL) );
	}

	
	genericPreProcEnterCriticalSection();


	if (!shader_state.shader_text)
	{
		ErrorFilenamef(filename, "Error loading %s", filename);
		errorIsDuringDataLoadingDec();
		genericPreProcReset();
		genericPreProcLeaveCriticalSection();

	}
	else
	{
		rdrShaderGetDebugNameAndHeader(SAFESTR2(debug_fn), SAFESTR2(debug_header), "shaders_processed", commentmarker, filename, strrchr(filename, '.'), true);
	
		errorIsDuringDataLoadingDec();

		if (!shader_state.shader_from_cache)
		{
			int include_guard = 0;

			PERFINFO_AUTO_START("genericPreProcIncludes", 1);
			while (genericPreProcIncludes(&shader_state.shader_text, path, filename, file_list, PreProc_UseCRCs) && include_guard < MAX_INCLUDE_DEPTH)
				include_guard++; // loop until done including files

			// "if"s are now being processed before macros to make it so macros may be nested within if statements.  By its nature, the other way around should still work.
			{
				char *strtokcontext=NULL;

				PERFINFO_AUTO_START("shaderPreProcessIfDefs", 1);
				genericPreProcIfDefs(shader_state.shader_text, &strtokcontext, 0, filename);
				PERFINFO_AUTO_STOP();
			}

			PERFINFO_AUTO_STOP_START("genericPreProcMacros", 1);
			genericPreProcMacros(&shader_state.shader_text);
			PERFINFO_AUTO_STOP_START("loadProgramCacheStore", 1);


			loadProgramCacheStore(filename, shader_state.shader_text);
			PERFINFO_AUTO_STOP();
		}

		// Do this after caching
		*new_crc = hashShader(shader_state.shader_text);

		genericPreProcReset();
		genericPreProcLeaveCriticalSection();


		fixCRLF(shader_state.shader_text);

		if (rdr_state.writeProcessedShaders)
		{
			writeDebugShaderData("shaders_processed", debug_fn, debug_header, shader_state.shader_text);
		}
		if (filename && rdr_state.writeProcessedShaders) {
			char override_name[MAX_PATH];
			fileLocateWrite(debug_fn, override_name);
			// Check for an override to load
			// Could use this for production-time pre-processing of shaders, although
			//   we would have to deal with a single graph having multiple LODs, and
			//   do this earlier in the chain of events.
			strstriReplace(override_name, "_processed", "_override"); // "shaders_override"
			if (fileExists(override_name)) {
				int len;
				char *data = fileAlloc(override_name, &len);
				if (data) {
					SAFE_FREE(shader_state.shader_text);
					shader_state.shader_text = data;
					*new_crc = hashShader(shader_state.shader_text);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return shader_state.shader_text;
}

void rdrFreeShaderData(void)
{
	if (shader_state.shader_text)
	{
		if (shader_state.shader_from_cache)
			free(shader_state.shader_text);
		else
			fileFree(shader_state.shader_text);
	}

	shader_state.shader_text = 0;
	shader_state.shader_from_cache = false;
}

void rdrPreProcessShader_dbg(char **text, const char *path, const char *debug_name, const char *ext, const char *commentmarker, U32 cached_crc, U32 *new_crc, FileList *file_list,
						 char *debug_fn, int debug_fn_size, char *debug_header, int debug_header_size MEM_DBG_PARMS)
{
	char *strtokcontext=NULL;
	int include_guard = 0;

	PERFINFO_AUTO_START_FUNC();

	if (debug_name) {
		rdrShaderGetDebugNameAndHeader(SAFESTR2(debug_fn), SAFESTR2(debug_header), "shaders_processed/dyn", commentmarker, debug_name, ext, false);
	}

	assert(strlen(commentmarker) <= 2);

	genericPreProcEnterCriticalSection();

	if (rdr_state.stripDeadShaderCode)
		genericPreProcSetCommentMarkers('\0', '\0');
	else
		genericPreProcSetCommentMarkers(commentmarker[0], commentmarker[1]);

	while (genericPreProcIncludes(text, path, NULL, file_list, PreProc_UseCRCs) && include_guard < MAX_INCLUDE_DEPTH)
		include_guard++; // loop until done including files
	genericPreProcIfDefs(*text, &strtokcontext, 0, NULL);
	genericPreProcMacros_dbg(text MEM_DBG_PARMS_CALL);
	*new_crc = hashShader(*text);
	//if (cached_crc != *new_crc) {
	//	genericPreProcIfDefs(*text, &strtokcontext, 0, NULL);
	//}
	fixCRLF(*text);
	if (debug_name && rdr_state.writeProcessedShaders) {
		writeDebugShaderData("shaders_processed/dyn", debug_fn, debug_header, *text);
	}
	if (debug_name && rdr_state.writeProcessedShaders) {
		char override_name[MAX_PATH];
		fileLocateWrite(debug_fn, override_name);
		// Check for an override to load
		// Could use this for production-time pre-processing of shaders, although
		//   we would have to deal with a single graph having multiple LODs, and
		//   do this earlier in the chain of events.
		strstriReplace(override_name, "_processed", "_override"); // "shaders_override"
		if (fileExists(override_name)) {
			int len;
			char *data = fileAlloc(override_name, &len);
			if (data) {
				SAFE_FREE(*text);
				*text = data;
				*new_crc = hashShader(*text);
			}
		}
	}
	
	genericPreProcReset();
	genericPreProcLeaveCriticalSection();

	PERFINFO_AUTO_STOP();
}

void loadRequiredDXDLL(char *path)
{
	HANDLE hDLL;
	char searchpath[MAX_PATH];
	hDLL = LoadLibrary_UTF8(path);
	if (!hDLL)
	{
		sprintf(searchpath,"../../3rdparty/DirectX/%s", path); 
		hDLL = LoadLibrary_UTF8(searchpath);
	}
	if (!hDLL)
		FatalErrorf("Failed to load %s.  Verifying Files in the launcher or reinstalling may fix this issue.", path);
}

void rdrShaderLibLoadDLLs(void)
{
	static bool doneOnce=false;
	if (doneOnce)
		return;
	doneOnce = true;

	// Attempt to load DLLs
	loadRequiredDXDLL("D3DX9_42.dll");
	loadRequiredDXDLL("D3DCompiler_42.dll");
}


void rdrShaderLibShutdown(RdrDevice *rdr_device)
{
	rdrDestroyDevice(rdr_device);
	dynamicCacheSafeDestroy(&shader_state.precompiled_cache);
}

void rdrShaderLibInit(void)
{
	char filename[MAX_PATH];

	static bool doneOnce=false;
	if (doneOnce)
		return;
	doneOnce = true;

	rdrShaderLibLoadDLLs();

#if _XBOX

    rdrShaderCacheFiles();
	sprintf(filename, "%s/rdrShaderCache.hogg", fileCacheDir());
	if (rdr_state.wipeShaderCache)
		fileForceRemove(filename);

    #define SRC_CACHE "platformbin/Xbox/shaderbin/rdrShaderCache.hogg"

#ifdef SLOW_CACHE_MERGE
	shader_state.precompiled_cache = dynamicCacheCreate(filename,
		RDR_SHADER_CACHE_VERSION, 4*1024*1024, 8*1024*1024, 1*3600, DYNAMIC_CACHE_DEFAULT);
	dynamicCacheMergePrecise(shader_state.precompiled_cache, SRC_CACHE, false);
#else
	shader_state.precompiled_cache = dynamicCacheMergeQuick(filename, SRC_CACHE, NULL,
		RDR_SHADER_CACHE_VERSION, 4*1024*1024, 8*1024*1024, 1*3600, DYNAMIC_CACHE_DEFAULT);
	if (isDevelopmentMode())
		dynamicCacheVerifyHogg(shader_state.precompiled_cache);
#endif

#else
	{

	rdrShaderCacheFiles();
	sprintf(filename, "%s/rdrShaderCache.hogg", fileCacheDir());
	if (rdr_state.wipeShaderCache)
		fileForceRemove(filename);

    #define SRC_CACHE "platformbin/PC/shaderbin/rdrShaderCache.hogg"

#ifdef SLOW_CACHE_MERGE
	shader_state.precompiled_cache = dynamicCacheCreate(filename,
		RDR_SHADER_CACHE_VERSION, 4*1024*1024, 8*1024*1024, 1*3600, DYNAMIC_CACHE_DEFAULT);
	dynamicCacheMergePrecise(shader_state.precompiled_cache, SRC_CACHE, false);

#else
	shader_state.precompiled_cache = dynamicCacheMergeQuick(filename, SRC_CACHE, NULL,
		RDR_SHADER_CACHE_VERSION, 4*1024*1024, 8*1024*1024, 1*3600, DYNAMIC_CACHE_DEFAULT);
	if (isDevelopmentMode())
		dynamicCacheVerifyHogg(shader_state.precompiled_cache);

#endif
	}
#endif
}

#undef rdrShaderPreloadLog
void rdrShaderPreloadLog(const char *fmt, ...)
{
	va_list va;
	char buf[4096]={0};
	if (!rdr_state.echoShaderPreloadLog || !rdrStatusPrintf)
		return;

	va_start(va, fmt);
	vsprintf(buf, fmt, va);
	va_end(va);
	rdrStatusPrintf("%s", buf);
	printf("rdrShaderPreloadLog: %s\n", buf);
}

bool rdrShaderPreloadSkip(const char *filename)
{
	return (strstri(filename,"SkipPreload")
            || strStartsWith(filename,"materials/Templates/system/Terrain")
            || strStartsWith(filename,"materials/Templates/Core_Env/WaterVolume")
			|| stricmp(filename, "shaders/D3D/default.phl")==0);
}

bool rdrShaderPreloadSkipEvenCompiling(const char *filename)
{
	char buf[MAX_PATH];
	getFileNameNoExt(buf, filename);
	if (stricmp(buf, "TerrainDebug")==0 ||
		stricmp(buf, "TerrainEdge")==0 ||
		stricmp(buf, "DefaultTemplate")==0 ||
		stricmp(buf, "ErrorTemplate")==0 ||
		stricmp(buf, "ConstColor0")==0 ||
		stricmp(buf, "OverrideColor")==0)
	{
		return true;
	}
	return false;
}

void rdrShaderSetTestDefine(int index, const char *str)
{
	assert(index>=0);
	rdrShaderEnterCriticalSection();
	while (index >= eaSize(&shader_state.shaderTestDefines))
		eaPush(&shader_state.shaderTestDefines, NULL);
	shader_state.shaderTestDefines[index] = str?allocAddCaseSensitiveString(str):NULL;
	rdrShaderLeaveCriticalSection();
}

int rdrShaderGetTestDefineCount(void)
{
	int ret;
	rdrShaderEnterCriticalSection();
	ret = eaSize(&shader_state.shaderTestDefines);
	rdrShaderLeaveCriticalSection();
	return ret;
}

const char *rdrShaderGetTestDefine(int index)
{
	const char *ret;
	rdrShaderEnterCriticalSection();
	ret = eaGet(&shader_state.shaderTestDefines, index);
	rdrShaderLeaveCriticalSection();
	return ret;
}

void rdrShaderSetGlobalDefine(int index, const char *str)
{
	assert(index>=0);
	rdrShaderEnterCriticalSection();
	while (index >= eaSize(&shader_state.shaderGlobalDefines))
		eaPush(&shader_state.shaderGlobalDefines, NULL);
	shader_state.shaderGlobalDefines[index] = str?allocAddCaseSensitiveString(str):NULL;
	rdrShaderLeaveCriticalSection();
}

int rdrShaderGetGlobalDefineCount(void)
{
	int ret;
	rdrShaderEnterCriticalSection();
	ret = eaSize(&shader_state.shaderGlobalDefines);
	rdrShaderLeaveCriticalSection();
	return ret;
}

const char *rdrShaderGetGlobalDefine(int index)
{
	const char *ret;
	rdrShaderEnterCriticalSection();
	ret = eaGet(&shader_state.shaderGlobalDefines, index);
	rdrShaderLeaveCriticalSection();
	return ret;
}
