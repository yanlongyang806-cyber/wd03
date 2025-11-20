#ifndef _RDRSHADERPRIVATE_H_
#define _RDRSHADERPRIVATE_H_

#include "RdrShader.h"

typedef struct FileEntry FileEntry;
typedef FileEntry** FileList;

void rdrShaderEnterCriticalSection(void);
void rdrShaderAssertInCriticalSection(void);

void rdrShaderResetCache(bool shouldDoCaching);

char *rdrLoadShaderData(const char* filename, const char *path, const char *commentmarker, U32 cached_crc, U32 *new_crc, FileList *file_list,
						char *debug_fn, int debug_fn_size, char *debug_header, int debug_header_size);
void rdrFreeShaderData(void);

bool rdrLoadPrecompiledShaderSync(SA_PARAM_NN_STR const char *filename, SA_PARAM_NN_STR const char *entrypoint, SA_PRE_NN_FREE SA_POST_NN_VALID void **data, SA_PRE_NN_FREE SA_POST_NN_VALID int *data_size, SA_PARAM_NN_STR const char *device_id);
void rdrSavePrecompiledShader(SA_PARAM_NN_STR const char *filename, SA_PARAM_NN_STR const char *entrypoint, SA_PRE_NN_BYTES_VAR(data_size) SA_POST_NN_VALID void *data, int data_size, SA_PARAM_OP_VALID FileList *file_list, SA_PARAM_NN_STR const char *device_id);

bool rdrFindPrecompiledShaderSync_WillStall(SA_PARAM_NN_STR const char *filename, SA_PARAM_NN_STR const char *entrypoint, SA_PRE_NN_FREE SA_POST_NN_VALID void **data, SA_PRE_NN_FREE SA_POST_NN_VALID int *data_size, SA_PARAM_NN_STR const char *device_id, bool bUpdateTimestamp);

void rdrPreProcessShader_dbg(char **text, const char *path, const char *debug_name, const char *ext, const char *commentmarker, U32 cached_crc, U32 *new_crc, FileList *file_list,
						 char *debug_fn, int debug_fn_size, char *debug_header, int debug_header_size MEM_DBG_PARMS);
#define rdrPreProcessShader(text,path,debug_name,ext,commentmarker,cached_crc,new_crc,file_list,debug_fn,debug_header) rdrPreProcessShader_dbg(text,path,debug_name,ext,commentmarker,cached_crc,new_crc,file_list,SAFESTR(debug_fn),SAFESTR(debug_header) MEM_DBG_PARMS_INIT)

void rdrShaderLeaveCriticalSection(void);

#if _PS3
#define shader_dynamicCacheIsFileUpToDate(cache, fullname) 1
#define shader_dynamicCacheIsFileUpToDateSync_WillStall(cache, fullname) 1
#define shader_dynamicCacheFileExists(cache, fullname) dynamicCacheFileExists(cache, fullname)
#else
#define shader_dynamicCacheIsFileUpToDate(cache, fullname) dynamicCacheIsFileUpToDate(cache, fullname)
#define shader_dynamicCacheIsFileUpToDateSync_WillStall(cache, fullname) dynamicCacheIsFileUpToDateSync_WillStall(cache, fullname)
#define shader_dynamicCacheFileExists(cache, fullname) dynamicCacheFileExists(cache, fullname)
#endif

#endif //_RDRSHADERPRIVATE_H_

