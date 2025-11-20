
#define _WIN32_WINNT 0x0501	// Change this to the appropriate value to target other versions of Windows.
#define _CRT_SECURE_NO_WARNINGS 1
#include "XWrapper.h"
#include <Windows.h>
#include <WinDef.h>
#include <stdio.h>
#include <tchar.h>
#include <time.h>
#include <assert.h>
#include "d3dx9.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "xgraphics.lib")

#ifdef XWRAPPER_EXPORTS
#define XWRAPPER_API __declspec(dllexport)
#else
#define XWRAPPER_API __declspec(dllimport)
#endif

void XWrapperStartup(void)
{
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	//winSetHInstance(hModule);
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		XWrapperStartup();
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
    return TRUE;
}

extern "C" {

XWRAPPER_API int XWrapperGetSizeOfStruct(void)
{
    return sizeof(XWrapperCompileShaderData);
}

static const char tohex[] = "0123456789abcdef";

XWRAPPER_API int XWrapperCompileShader(XWrapperCompileShaderData *data)
{
	//static const char *dummy_updb_path = "ShaderDumpxe:\\updbs\\--\\01234567-01234567-01234567.updb";
	static const char *dummy_updb_path = "xe:\\updbs\\--\\01234567-01234567-01234567.updb";
	static const int path_len = (int)strlen(dummy_updb_path);
	int offs_dir = (int)(strchr(dummy_updb_path, '-') - dummy_updb_path);
	int offs_hint = (int)(strchr(dummy_updb_path, '0') - dummy_updb_path);
	char real_updb_path[MAX_PATH];
	
    if (data->sizeOfStruct != sizeof(*data))
	{
		printf("Mismatched version of XWrapper.dll");
		return 0;
	}
    data->compiledResult = 0;

	bool bGenerateUPDB = !!data->updbPath;

	D3DXSHADER_COMPILE_PARAMETERS d3dxParams = { 0 };
	LPD3DXBUFFER compiledResult = NULL, errormsgs = NULL;

	d3dxParams.UPDBTimestamp = (DWORD)time(NULL);
	d3dxParams.Flags = D3DXSHADEREX_GENERATE_UPDB;
	d3dxParams.UPDBPath = dummy_updb_path;

#define COMPILE_SHADER(flags)	\
	D3DXCompileShaderEx(data->programText, (UINT)data->programTextLen, NULL, NULL,	\
		data->entryPointName, data->shaderModel, (flags), &compiledResult, &errormsgs, NULL,					\
		(!bGenerateUPDB || (flags&D3DXSHADER_MICROCODE_BACKEND_OLD_DEPRECATED))?NULL:&d3dxParams)
	if (FAILED(COMPILE_SHADER(0)))
	{
		if (errormsgs) {
			char *errormessage = (char*)errormsgs->GetBufferPointer();
			if (data->errorBuffer)
				strcpy_s(data->errorBuffer, data->errorBuffer_size, errormessage);
			errormsgs->Release();
			errormsgs = NULL;
		}

        if(data->errDumpLocation) {
            FILE *fp = fopen(data->errDumpLocation, "wb");
            if(fp) {
                fwrite(data->programText, 1, data->programTextLen, fp);
                fclose(fp);
            }
        }

// Disabling this because it causes confusion and also doesn't clear/set the UPDB info correctly
// 		// Try again with the "old" compiler
// 		if (FAILED(COMPILE_SHADER(D3DXSHADER_MICROCODE_BACKEND_OLD_DEPRECATED)))
// 		{
// 			// Failed again, leave the original error message
// 		} else {
// 			// Success
// 			data->errorBuffer[0] = '\0';
// 		}
	} else {
		// Success
	}

	if (compiledResult) {
		data->compiledResultLen = compiledResult->GetBufferSize();
		data->compiledResult = data->allocFunc(data->compiledResultLen);
		memcpy(data->compiledResult, compiledResult->GetBufferPointer(), data->compiledResultLen);

		if (data->writeDisassembledPath)
		{
			LPD3DXBUFFER buffer = NULL;
			D3DXDisassembleShader((DWORD*)data->compiledResult, FALSE, NULL, &buffer);
			if (buffer)
			{
				const char *assembly = (const char *)buffer->GetBufferPointer();
				FILE *f;
				f = fopen(data->writeDisassembledPath, "w");
				if (!f) {
					printf("Error opening %s for writing.\n", data->writeDisassembledPath);
				} else {
					fwrite(assembly, 1, strlen(assembly), f);
					fclose(f);
				}
				buffer->Release();
				buffer = NULL;
			}
		}
	}

	if (bGenerateUPDB && d3dxParams.pUPDBBuffer)
	{
		assert(data->compiledResult);

		// Return UPDB data
		data->updbDataLen = d3dxParams.pUPDBBuffer->GetBufferSize();
		data->updbData = data->allocFunc(data->updbDataLen);
		memcpy(data->updbData, d3dxParams.pUPDBBuffer->GetBufferPointer(), data->updbDataLen);

		unsigned char pdbhint[12];
		// Get PDB hint (GUID?)
		memcpy(pdbhint, (char*)data->compiledResult + data->compiledResultLen - 12, 12);
		// Get PDB hint from UPDB and make sure it matches
		char *pdb_hint_text = strstr((char*)data->updbData, "pdbHint=\"");
		assert(pdb_hint_text);
		pdb_hint_text += strlen("pdbHint=\"");
		// Make new path
		strcpy(real_updb_path, dummy_updb_path);
		real_updb_path[offs_dir] = tohex[pdbhint[11] >> 4];
		real_updb_path[offs_dir+1] = tohex[pdbhint[11] & 0xf];
		for (int i=0; i<12; i++) {
			real_updb_path[offs_hint + i*2 + i/4] = tohex[pdbhint[i] >> 4];
			real_updb_path[offs_hint + i*2 + i/4+1] = tohex[pdbhint[i] & 0xf];
		}
		assert(strncmp(real_updb_path+offs_hint, pdb_hint_text, 8*3+2)==0);
		// Patch up shader data with new UPDB path
		//    Find where the old path was written
		int oldlocation=-1;
		for (int i=0; i<data->compiledResultLen; i++) {
			if (memcmp((char*)data->compiledResult + i, dummy_updb_path, path_len)==0) {
				oldlocation = i;
				break;
			}
		}
		assert(oldlocation != -1);
		char *path = (char*)data->compiledResult + oldlocation;
		memcpy(path, real_updb_path, path_len);
		// Return UPDB path
		strcpy_s(data->updbPath, data->updbPath_size, real_updb_path);
	}

	if (compiledResult) {
		compiledResult->Release();
		compiledResult = NULL;
	}

	if (d3dxParams.pUPDBBuffer) {
		d3dxParams.pUPDBBuffer->Release();
		d3dxParams.pUPDBBuffer = NULL;
	}

	if (errormsgs) {
		errormsgs->Release();
		errormsgs = NULL;
	}

    return data->compiledResult!=0;
}

}
