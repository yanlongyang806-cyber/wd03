#pragma once

typedef void *(*Allocator)(size_t size);
typedef void (*Destructor)(void *data);

enum XWrapperFlags {

    XWRAPPER_VERBOSE = 1,
    XWRAPPER_SERVER = 1<<16,
};

typedef struct XWrapperCompileShaderData
{
	int sizeOfStruct;
	void *compiledResult; // Output
	int compiledResultLen; // Output
	const char *programText;
	int programTextLen;
	const char *entryPointName;
	const char *shaderModel;
	char *updbPath; // Filled in if non-NULL.  No UPDB info created if NULL
	int updbPath_size;
	int flags;
    int compilerFlags;
	void *updbData; // Output
	int updbDataLen; // Output
	char *errorBuffer; // Filled in if non-NULL
	int errorBuffer_size;
	Allocator allocFunc;
	Destructor destructorFunc;
	char *writeDisassembledPath;
    const char *errDumpLocation;
} XWrapperCompileShaderData;

typedef int (*tpfnXWrapperCompileShader)(XWrapperCompileShaderData *data);

