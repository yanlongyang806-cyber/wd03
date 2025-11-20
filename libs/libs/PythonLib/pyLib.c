#include "pyLib.h"

#include "earray.h"
#include "file.h"
#include "resource_pylib.h"
#include "StashTable.h"
#include "timing_profiler.h"
#include "timing_profiler_interface.h"
#include "utils.h"
#include "windefinclude.h"
#include "winutil.h"
#include "UTF8.h"

static char pcPythonHomeDir[CRYPTIC_MAX_PATH] = "";
static char pcPythonScriptDir[CRYPTIC_MAX_PATH] = "";

static bool py_debug = 0;
StashTable spy_debug_profiling = NULL;

AUTO_CMD_INT(py_debug, pydebug) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

static bool sbPythonHomeDirSet = false;
static bool sbPythonHookImports = false;
static PyObject *spMainModule = NULL;

PyObject *PyLib_None = NULL;

typedef struct PyInitVar
{
	char *pcVarName;
	char *pcValue;
	bool bAppend;
} PyInitVar;
static PyInitVar **sppPyInitVars = NULL;

AUTO_COMMAND ACMD_NAME(PySet) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void pyLibInitSet(const char *pcVarName, const char *pcValue)
{
	PyInitVar *pVar = calloc(1, sizeof(PyInitVar));

	pVar->pcVarName = strdup(pcVarName);
	pVar->pcValue = forwardSlashes(strdup(pcValue));
	pVar->bAppend = false;

	eaPush(&sppPyInitVars, pVar);
}

AUTO_COMMAND ACMD_NAME(PyAppend) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void pyLibInitAppend(const char *pcVarName, const char *pcValue)
{
	PyInitVar *pVar = calloc(1, sizeof(PyInitVar));

	pVar->pcVarName = strdup(pcVarName);
	pVar->pcValue = forwardSlashes(strdup(pcValue));
	pVar->bAppend = true;

	eaPush(&sppPyInitVars, pVar);
}

AUTO_COMMAND ACMD_NAME(PyRun) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void pyLibInitRun(const char *pcScriptString)
{
	PyInitVar *pVar = calloc(1, sizeof(PyInitVar));

	pVar->pcVarName = strdup(pcScriptString);
	pVar->pcValue = NULL;
	pVar->bAppend = false;

	eaPush(&sppPyInitVars, pVar);
}

void pyLibInitVars(void)
{
	int i;

	for(i = eaSize(&sppPyInitVars) - 1; i >= 0; --i)
	{
		PyInitVar *pInitVar = eaRemove(&sppPyInitVars, i);
		char *pPyCmd = NULL;

		if(!pInitVar->pcValue)
		{
			if(!PyRun_SimpleString(pInitVar->pcVarName))
			{
				PyErr_Print();
			}
		}
		else if(pInitVar->bAppend)
		{
			estrPrintf(&pPyCmd, "try:\n    __T = (%s,)\nexcept NameError:\n    __T = ('%s',)\n", pInitVar->pcValue, pInitVar->pcValue);

			if(PyRun_SimpleString(pPyCmd))
			{
				estrPrintf(&pPyCmd, "try:\n    %s += __T\nexcept NameError:\n    %s = list(__T)\n", pInitVar->pcVarName, pInitVar->pcVarName);
				
				if(!PyRun_SimpleString(pPyCmd))
				{
					PyErr_Print();
				}
			}
			else
			{
				PyErr_Print();
			}

			estrDestroy(&pPyCmd);
		}
		else
		{
			estrPrintf(&pPyCmd, "try:\n    %s = %s\nexcept NameError, SyntaxError:\n    %s = '%s'", pInitVar->pcVarName, pInitVar->pcValue, pInitVar->pcVarName, pInitVar->pcValue);

			if(!PyRun_SimpleString(pPyCmd))
			{
				PyErr_Print();
			}

			estrDestroy(&pPyCmd);
		}
	}
}

// Set the Python home directory, which should contain the DLL and Python standard lib
void pyLibSetHomeDir(const char *pcHomeDir)
{
	if(fileIsAbsolutePath(pcHomeDir) || strStartsWith(pcHomeDir, fileBaseDir()))
	{
		strcpy(pcPythonHomeDir, pcHomeDir);
	}
	else
	{
		sprintf(pcPythonHomeDir, "%s/%s", fileBaseDir(), pcHomeDir);
	}

	sbPythonHomeDirSet = true;
}

// Set the Python script directory, which should contain any scripts/modules to be used
void pyLibSetScriptDir(const char *pcScriptDir)
{
	strcpy(pcPythonScriptDir, pcScriptDir);
}

static void removeCRs(char *text)
{
	char *src = text;
	char *dst = text;

	while(*src)
	{
		if(*src != '\r')
		{
			*dst = *src;
			dst++;
		}
		src++;
	}
	*dst = 0;
}

static char *pyLibFixupPath(const char *pcFilename, const char *pcDir)
{
	char *pcOutPath = NULL;

	if(fileIsAbsolutePath(pcFilename))
	{
		estrPrintf(&pcOutPath, "%s", pcFilename);
	}
	else if(strStartsWith(pcFilename, pcDir))
	{
		estrPrintf(&pcOutPath, "%s", pcFilename);
	}
	else
	{
		estrPrintf(&pcOutPath, "%s/%s", pcDir, pcFilename);
	}

	if(!strEndsWith(pcFilename, ".py"))
	{
		estrAppend2(&pcOutPath, ".py");
	}

	return pcOutPath;
}

static PyObject *pyLibModuleExists(PyObject *self, PyObject *args)
{
	char *pcInPath = NULL, *pcFixedPath = NULL;
	bool bExists = false;

	if(!PyArg_ParseTuple(args, "s", &pcInPath))
	{
		PyErr_BadArgument();
		return NULL;
	}

	pcFixedPath = pyLibFixupPath(pcInPath, pcPythonScriptDir);
	bExists = fileExists(pcFixedPath);
	estrDestroy(&pcFixedPath);
	return Py_BuildValue("b", bExists);
}

static PyObject *pyLibModuleLoad(PyObject *self, PyObject *args)
{
	char *pcInPath = NULL, *pcModuleName = NULL;
	PyObject *pModule = NULL;

	if(!PyArg_ParseTuple(args, "ss", &pcInPath, &pcModuleName))
	{
		PyErr_BadArgument();
		return NULL;
	}

	pModule = pyLibLoadScript(pcInPath, pcModuleName);

	if(!pModule)
	{
		return PyLib_IncNone;
	}

	return pModule;
}

static const char *pyLibProfileHelper(PyObject *self, PyObject *args)
{
	PERFINFO_TYPE **ppPerfInfo = NULL;
	const char *pcStashKey = NULL;
	char *pcFuncTimer = NULL;
	char *pcFuncFile, *pcFuncName, *pcCallerFile;
	int iCallerLine = 0;

	if(!PyArg_ParseTuple(args, "sssi", &pcFuncFile, &pcFuncName, &pcCallerFile, &iCallerLine))
	{
		PyErr_BadArgument();
		return NULL;
	}

	if(pcCallerFile[0])
	{
		estrPrintf(&pcFuncTimer, "%s:%s (%s:%d)", pcFuncFile, pcFuncName, pcCallerFile, iCallerLine);
	}
	else
	{
		estrPrintf(&pcFuncTimer, "%s:%s", pcFuncFile, pcFuncName);
	}

	if(!spy_debug_profiling)
	{
		spy_debug_profiling = stashTableCreateWithStringKeys(8, StashDeepCopyKeys_NeverRelease);
	}

	if(!stashFindElement(spy_debug_profiling, pcFuncTimer, NULL))
	{
		ppPerfInfo = calloc(1, sizeof(PERFINFO_TYPE *));
		stashAddPointer(spy_debug_profiling, pcFuncTimer, ppPerfInfo, true);
	}

	stashGetKey(spy_debug_profiling, pcFuncTimer, &pcStashKey);
	estrDestroy(&pcFuncTimer);
	return pcStashKey;
}

static PyObject *pyLibProfileBegin(PyObject *self, PyObject *args)
{
	PERFINFO_TYPE **ppPerfInfo = NULL;
	const char *pcStashKey;
	S32 didDisable = 0;

	autoTimerDisableRecursion(&didDisable);
	pcStashKey = pyLibProfileHelper(self, args);
	stashFindPointer(spy_debug_profiling, pcStashKey, &(void*)ppPerfInfo);
	autoTimerEnableRecursion(didDisable);
	
	PERFINFO_AUTO_START_STATIC(pcStashKey, ppPerfInfo, 1);
	
	return PyLib_IncNone;
}

static PyObject *pyLibProfileEnd(PyObject *self, PyObject *args)
{
// 	const char *pcStashKey;
// 	S32 didDisable = 0;
	
/*
	autoTimerDisableRecursion(&didDisable);
	pcStashKey = pyLibProfileHelper(self, args);
	autoTimerEnableRecursion(didDisable);
*/

	PERFINFO_AUTO_STOP();
	
	return PyLib_IncNone;
}

PyObject *pyLibLoadResource(int resID, const char *fn, const char *module)
{
	void *pData = NULL;
	PyObject *pCode = NULL;
	int len = 0;

	HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(resID), L"TXT");
	if (rsrc)
	{
		HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
		if (gptr)
		{
			pData = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
		}
	}

	if(!pData)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Resource %d (%s) did not load! (%x)\n", resID, fn, GetLastError());
		return NULL;
	}

	pCode = Py_CompileString(pData, fn, Py_file_input);

	if(!pCode)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Resource %d (%s) did not compile!\n", resID, fn);
		PyErr_Print();
		return NULL;
	}

	return PyImport_ExecCodeModule((char*)module, pCode);
}

static PyMethodDef PyLibMethods[] = {
	{"exists", pyLibModuleExists, METH_VARARGS, "Determine if a module exists in the Cryptic file system."},
	{"load", pyLibModuleLoad, METH_VARARGS, "Load a module using the Cryptic file system."},
	{"profile_begin", pyLibProfileBegin, METH_VARARGS, "Begin profiling a function call with the specified parameters."},
	{"profile_end", pyLibProfileEnd, METH_VARARGS, "Stop profiling a function call. Use the parameters to verify timer balance."},
	{NULL, NULL, 0, NULL}
};

// Initializes the global Python environment - in particular, loads the DLL from the right place and creates the cryptic module
bool pyLibInitialize()
{
	char pcPythonLibPath[1024];
	char pcMSVCRLibPath[1024];
	PyObject *pCrypticModule = NULL, *pCrypticPath = NULL;

	if(!sbPythonHomeDirSet)
	{
#ifdef _WIN64
		sprintf(pcPythonHomeDir, "%s/tools/Python/x64", fileBaseDir());
#else
		sprintf(pcPythonHomeDir, "%s/tools/Python/x86", fileBaseDir());
#endif
	}

	sprintf(pcMSVCRLibPath, "%s/msvcr71.dll", pcPythonHomeDir);
	sprintf(pcPythonLibPath, "%s/python25.dll", pcPythonHomeDir);
	backSlashes(pcMSVCRLibPath);
	backSlashes(pcPythonLibPath);

#ifndef _WIN64
	if(!LoadLibrary_UTF8(pcMSVCRLibPath) && !LoadLibrary(L"msvcr71.dll"))
	{
		printf("Failed to load msvcr71.dll: %d\n", GetLastError());
		return false;
	}
#endif

	if(!LoadLibrary_UTF8(pcPythonLibPath) && !LoadLibrary(L"python25.dll"))
	{
		printf("Failed to load python25.dll: %d\n", GetLastError());
		return false;
	}

	Py_SetPythonHome(pcPythonHomeDir);
	Py_Initialize();

	PyLib_None = Py_BuildValue("");
	Py_IncRef(PyLib_None);

	// Set up the cryptic module
	pCrypticModule = Py_InitModule("cryptic", PyLibMethods);
	pCrypticPath = PyList_New(0);
	PyModule_AddObject(pCrypticModule, "__path__", pCrypticPath);
	PyModule_AddStringConstant(pCrypticModule, "__file__", pcPythonScriptDir);

	// Hook imports
	pyLibLoadResource(IMPORTHOOK, "<import hook>", "__importhook");
	if(py_debug)
		pyLibLoadResource(PROFILEHOOK, "<profile hook>", "__profilehook");

	return true;
}

void pyLibFinalize(void)
{
	Py_DecRef(PyLib_None);
	Py_Finalize();
}

// Loads a script file with the given module name
PyObject *pyLibLoadScript(const char *fn, const char *module)
{
	PyObject *pModule = NULL, *pCode = NULL;
	char *pcFixedPath = NULL;
	char *f = NULL;
	int len = 0;

	pcFixedPath = pyLibFixupPath(fn, pcPythonScriptDir);
	f = fileAlloc(pcFixedPath, &len);

	if(!f)
	{
		estrDestroy(&pcFixedPath);
		return NULL;
	}

	removeCRs(f);
	pCode = Py_CompileString(f, pcFixedPath, Py_file_input);
	estrDestroy(&pcFixedPath);
	fileFree(f);

	if(!pCode)
	{
		PyErr_Print();
		return NULL;
	}

	pModule = PyImport_ExecCodeModule((char*)module, pCode);
	Py_XINCREF(pModule);

	if(!pModule)
	{
		PyErr_Print();
		return NULL;
	}

	return pModule;
}

PyObject *pyLibGetFuncSafe(PyObject *pModule, const char *name)
{
	if(PyObject_HasAttrString(pModule, name))
	{
		PyObject *pFunc = PyObject_GetAttrString(pModule, name);
		if(PyCallable_Check(pFunc))
		{
			return pFunc;
		}
		else
		{
			Py_XDECREF(pFunc);
			return NULL;
		}
	}
	return NULL;
}

void pyLibExecute(const char *script)
{
	if(!PyRun_SimpleString(script))
	{
		PyErr_Print();
	}
}