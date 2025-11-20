// This library contains functions to provide embedded Python support in Cryptic apps. To link it:
//     1) For your project, open Project Dependencies... and select PythonLib.
//     2) For your project, open Properties > Linker > Input and add "python25.dll" under Delay-Loaded DLLs.

// In order to link python25.dll, Python must be installed on the machine which will run your app.
// Anybody who has full checkouts of Core/tools/ or [any game]/tools/ will have the appropriate Python
// DLLs and standard library files already. Also, if you patch to any production build of a game server
// project, you will get the appropriate Python files. Otherwise, go to http://www.python.org/ to download.

// Here's an example use pattern in code. This example will initialize the Python environment, run a
// specified script one time, and then clean up the Python environment. If we wanted to have the environment
// persist, we'd simply skip the call to pyLibFinalize() until we were ready to do so.
//
//     void initExamplePythonEnvironment(const char *script_path)
//     {
//         if(!pyLibInitialize()) return;
//         pyLibLoadScript(script_path, PYLIB_MAIN_MODULE);
//         pyLibFinalize();
//     }

#define HAVE_SNPRINTF
#undef _DEBUG
#include "Python.h"
#define _DEBUG

// This is the name of the main module
#define PYLIB_MAIN_MODULE "__main__"

// A pointer we define locally to reference the None object
extern PyObject *PyLib_None;
#define PyLib_IncNone (Py_IncRef(PyLib_None), PyLib_None)

// Sets a custom location for the Python DLL and standard library - otherwise, looks in fileBaseDir()/tools/Python
void pyLibSetHomeDir(const char *pcHomeDir);

// Sets a location to look for any scripts or modules you try to load. Any Cryptic file path is valid.
void pyLibSetScriptDir(const char *pcScriptDir);

// Sets up variables to initialize in the main module after loading
void pyLibInitSet(const char *pcVarName, const char *pcValue);
void pyLibInitAppend(const char *pcVarName, const char *pcValue);
void pyLibInitRun(const char *pcScriptString);
void pyLibInitVars(void);

bool pyLibInitialize(void); // Initialize the Python environment
void pyLibFinalize(void); // Clean up the Python environment

// Loading functions - Return the Python object corresponding to the loaded module - use this to access functions or other data
// For 'module', specify PYLIB_MAIN_MODULE if you're loading your entry point script
PyObject *pyLibLoadScript(const char *fn, const char *module); // Loads a script from a file
PyObject *pyLibLoadResource(int resID, const char *fn, const char *module); // Loads a script from a resource in the EXE - fn is just for informational purposes

// Convenience function to get a Python function out of a loaded module
PyObject *pyLibGetFuncSafe(PyObject *pModule, const char *name);

// Convenience function to execute a raw script string
void pyLibExecute(const char *script);

// Utility macros for defining APIs more easily
#define PY_FUNC(name) PyObject *name(PyObject *self, PyObject *args)
#define PY_FUNC_DEF(name, alias, doc) {alias, name, METH_VARARGS, doc}
#define PY_FUNC_TERM {NULL, NULL, 0, NULL}
#define PY_PARSE_ARGS(types, ...) \
	if(!PyArg_ParseTuple(args, types, __VA_ARGS__)) \
	{ \
		PyErr_BadArgument(); \
		return NULL; \
	}
#define PY_PARSE_ARGS_PERF_STOP(types, ...) \
	if(!PyArg_ParseTuple(args, types, __VA_ARGS__)) \
	{ \
		PyErr_BadArgument(); \
		PERFINFO_AUTO_STOP(); \
		return NULL; \
	}