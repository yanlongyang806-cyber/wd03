#ifndef _PATCHCOMMONUTILS_H
#define _PATCHCOMMONUTILS_H

#define PATCH_MAX_PATH				200			// A pathname may not exceed this length.

U32 patchChecksum(void *data,U32 len);
U32 patchChecksumFile(SA_PARAM_NN_STR const char *file);
U32 getCurrentFileTime(void);
#define patchFileTimeToSS2000 timePatchFileTimeToSecondsSince2000
#define patchSS2000ToFileTime timeSecondsSince2000ToPatchFileTime
void stripPath(char * path, const char * stripped);
void fixPath_s(char * path, size_t path_size, const char * stripped);
#define fixPath(path,stripped) fixPath_s(SAFESTR(path),stripped)
void machinePath_s(char *adjusted, size_t adjusted_size, const char *path);
#define machinePath(adjusted,path) machinePath_s(SAFESTR(adjusted),path)

int patchRenameWithAlert_dbg(const char *source, const char *dest MEM_DBG_PARMS);
#define patchRenameWithAlert(source, dest) patchRenameWithAlert_dbg(source, dest MEM_DBG_PARMS_INIT)

// Parse an HTTP info string.
bool patchParseHttpInfo(const char *http_info, char *server, size_t server_size, U16 *port, char *prefix, size_t prefix_size);

#endif
