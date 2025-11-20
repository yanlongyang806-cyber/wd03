#pragma once
GCC_SYSTEM

// fileLoader.h
// This is intended to evolve into the central place that all file
// loading occurs on the client, as follows:
//   a) All file loads must be queued up asynchronously
//   b) fileLoader can rearrange order of file requests for optimal
//      disk access speeds
//   c) Callers can request background execs of arbitrary functions,
//      but these should be limited to hard drive reads only
//   d) Some files may need to be patched before they can be loaded
//      and this should happen transparently through fileLoader
//   e) Only file loads/disk reads should be queued to this, *not*
//      CPU-intensive uncompressing steps, etc

// fileLoader functions can be called from *any* thread
// the callbacks passed to fileLoaderRequestAsyncLoad will only be called from the main thread (who calls fileLoaderCheck())
// the function passed to fileLoaderRequestAsyncExec obviously is run in the background
// if the caller simply needs the file patched but not loaded (e.g. to hand over
//   to some other loading system like FMOD), call fileLoaderRequestAsyncExec
//   and have the callback simply notify the caller's system that the file is ready

typedef struct HogFile HogFile;

typedef enum FileLoaderPriority
{
	FILE_LOWEST_PRIORITY,
	FILE_LOW_PRIORITY,
	FILE_MEDIUM_PRIORITY,
	FILE_MEDIUM_HIGH_PRIORITY,
	FILE_HIGH_PRIORITY,
	FILE_HIGHEST_PRIORITY,
} FileLoaderPriority;

void fileLoaderInit(void);

typedef void (*FileLoaderFunc)(const char *filename, void *data, int dataSize, void *userData);
void fileLoaderRequestAsyncLoad(const char *filename, FileLoaderPriority priority, FileLoaderFunc callback, void *userData);

// loads compressed data directly from a hogg, if available, and uncompressed data if uncompressed is set
// or compressed data is unavailable
typedef void (*FileLoaderHoggFunc)( const char *filename, void *uncompressed, int uncompressedSize,
									void *compressed, int compressedSize, U32 crc, void *userData );
void fileLoaderRequestAsyncLoadFromHogg(HogFile *hogg, const char *filename, FileLoaderPriority priority, bool uncompressed,
										FileLoaderHoggFunc background, FileLoaderHoggFunc foreground, void *userData);

typedef void (*FileLoaderExecFunc)(const char *filename, void *userData);
void fileLoaderRequestAsyncExec(const char *filename, FileLoaderPriority priority, bool dontCount, FileLoaderExecFunc callback, void *userData);

void fileLoaderCheck(void);
int fileLoaderLoadsPending(void);
int fileLoaderPatchesPending(void);


typedef enum PatchStreamingStartResult
{
	PSSR_NotNeeded, // this path is not to a file that needs patching
	PSSR_Started, // started just now
	PSSR_AlreadyStarted, // was started previously
	PSSR_Full, // xfers are full, call back later
} PatchStreamingStartResult;

typedef PatchStreamingStartResult(*PatchStreamingRequestPatchCallback)(const char *relpath);
typedef bool (*PatchStreamingNeedsPatchingCallback)(const char *relpath);
typedef void (*PatchStreamingProcessCallback)(void);
void fileLoaderSetPatchStreamingCallbacks(PatchStreamingRequestPatchCallback req, PatchStreamingNeedsPatchingCallback needs, PatchStreamingProcessCallback proc);

extern PatchStreamingRequestPatchCallback fileloader_patch_req_callback;
extern PatchStreamingNeedsPatchingCallback fileloader_patch_needs_callback;
extern PatchStreamingProcessCallback fileloader_patch_proc_callback;
