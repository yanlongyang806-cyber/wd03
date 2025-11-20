#include "pub/XWrapperInterface.h"
#include "wininclude.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););

#if !PLATFORM_CONSOLE

static HMODULE hXWrapperDLL;
static bool bXWrapperInited;
static tpfnXWrapperCompileShader compileFunc;
typedef int (*pfnGetGetSizeOfStruct)(void);

static void XWrapperInit(void)
{
	if (bXWrapperInited)
		return;
	hXWrapperDLL = LoadLibrary(L"XWrapper.dll");
	if (!hXWrapperDLL)
		hXWrapperDLL = LoadLibrary(L"../../libs/bin/XWrapper.dll");
	if (hXWrapperDLL) {
        pfnGetGetSizeOfStruct pFunc = (pfnGetGetSizeOfStruct)GetProcAddress(hXWrapperDLL, "XWrapperGetSizeOfStruct");
        if(pFunc && pFunc()==sizeof(XWrapperCompileShaderData)) {
		    compileFunc = (tpfnXWrapperCompileShader)GetProcAddress(hXWrapperDLL, "XWrapperCompileShader");
        }
        if(!compileFunc) {
            ErrorDeferredf("Failed to find valid XWrapper.dll, possibly version mismatch, not compiling XBox shaders.");
            FreeLibrary(hXWrapperDLL), hXWrapperDLL = 0;
        }
	} else {
		ErrorDeferredf("Failed to load XWrapper.dll, not compiling XBox shaders.");
	}
	bXWrapperInited = true;
}

static void freeFunc(char *str)
{
	free(str);
}

static void *allocFunc(size_t size)
{
	return malloc(size);
}

int XWrapperCompileShader(XWrapperCompileShaderData *data)
{
	if (!bXWrapperInited) {
		XWrapperInit();
	}
	if (compileFunc) {
		data->sizeOfStruct = sizeof(*data);
		data->allocFunc = allocFunc;
		data->destructorFunc = freeFunc;
		return compileFunc(data);
	}
    return 0;
}

#else

int symbol_to_make_linker_happy_2=0;

#endif