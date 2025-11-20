#include "TestClientPython.h"

#include "ClientControllerLib.h"
#include "cmdparse.h"
#include "earray.h"
#include "logging.h"
#include "pyLib.h"
#include "TestClient.h"
#include "TestClientScripting.h"
#include "tokenstore.h"
#include "utils.h"

#include "TestClientCommon_h_ast.h"

static PyObject *spMainModule = NULL;
static PyObject **sppCallbacks = NULL;

static PyObject *TestClient_Python_Log(PyObject *self, PyObject *args)
{
	const char *pcLogStr = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, "s", &pcLogStr))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	log_printf(LOG_TC_TESTCLIENT, "%s", pcLogStr);
	PERFINFO_AUTO_STOP();
	return PyLib_IncNone;
}

static PyObject *TestClient_Python_Exit(PyObject *self, PyObject *args)
{
	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	gTestClientGlobalState.bRunning = false;
	PERFINFO_AUTO_STOP();
	return PyLib_IncNone;
}

static PyObject *TestClient_Python_Execute(PyObject *self, PyObject *args)
{
	const char *pcCommand = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, "s", &pcCommand))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	ClientController_SendCommandToClient(pcCommand);
	PERFINFO_AUTO_STOP();
	return PyLib_IncNone;
}

static PyObject *TestClient_Python_LocalExecute(PyObject *self, PyObject *args)
{
	const char *pcCommand = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, "s", &pcCommand))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	globCmdParse(pcCommand);
	PERFINFO_AUTO_STOP();
	return PyLib_IncNone;
}

static PyObject *TestClient_Python_RegisterTick(PyObject *self, PyObject *args)
{
	PyObject *pCallback = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, "O", &pCallback) || !PyCallable_Check(pCallback))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	Py_XINCREF(pCallback);
	eaPush(&sppCallbacks, pCallback);
	PERFINFO_AUTO_STOP();
	return PyLib_IncNone;
}

static PyObject *TestClient_Python_GetScript(PyObject *self, PyObject *args)
{
	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return Py_BuildValue("s", gTestClientGlobalState.cScriptName);
}

static PyObject *TestClient_Python_GetTime(PyObject *self, PyObject *args)
{
	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return Py_BuildValue("f", TestClient_GetScriptTime());
}

static PyObject *TestClient_Python_GetID(PyObject *self, PyObject *args)
{
	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return Py_BuildValue("i", gTestClientGlobalState.iID);
}

static PyObject *TestClient_Python_GetFPS(PyObject *self, PyObject *args)
{
	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return Py_BuildValue("i", gTestClientGlobalState.iFPS);
}

static PyObject *TestClient_Python_GetExeState(PyObject *self, PyObject *args)
{
	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return Py_BuildValue("i", ClientController_MonitorState());
}

static PyObject *TestClient_Python_GetFSMState(PyObject *self, PyObject *args)
{
	PERFINFO_AUTO_START_FUNC();

	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_BadArgument();
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return Py_BuildValue("s", ClientController_GetClientFSMState());
}

static PyMethodDef TestClientMethods[] = {
	{"log", TestClient_Python_Log, METH_VARARGS, "Send a string to the Log Server using the TC_TESTCLIENT log type."},
	{"exit", TestClient_Python_Exit, METH_VARARGS, "Finish execution of the Test Client and exit."},
	{"execute", TestClient_Python_Execute, METH_VARARGS, "Execute an auto command on the controlled Game Client."},
	{"local_execute", TestClient_Python_LocalExecute, METH_VARARGS, "Execute an auto command locally on the Test Client."},
	{"register_tick", TestClient_Python_RegisterTick, METH_VARARGS, "Register a tick function to be called every frame."},
	{"get_script", TestClient_Python_GetScript, METH_VARARGS, "Get the name of the script passed to the Test Client."},
	{"get_time", TestClient_Python_GetTime, METH_VARARGS, "Get the time since the scripting environment started."},
	{"get_id", TestClient_Python_GetID, METH_VARARGS, "Get the container ID of the Test Client."},
	{"get_fps", TestClient_Python_GetFPS, METH_VARARGS, "Get the framerate of the Test Client."},
	{"client_exe_state", TestClient_Python_GetExeState, METH_VARARGS, "Get the current state of the controlled GameClient.exe."},
	{"client_fsm_state", TestClient_Python_GetFSMState, METH_VARARGS, "Get the current FSM state of the controlled Game Client."},
	{NULL, NULL, 0, NULL},
};

static void TestClient_RegisterPythonModule(void)
{
	PyObject *pModule = Py_InitModule("tc", TestClientMethods);

	PyModule_AddIntConstant(pModule, "NOT_RUNNING", CC_NOT_RUNNING);
	PyModule_AddIntConstant(pModule, "RUNNING", CC_RUNNING);
	PyModule_AddIntConstant(pModule, "CONNECTED", CC_CONNECTED);
	PyModule_AddIntConstant(pModule, "CRASHED", CC_CRASHED);
	PyModule_AddIntConstant(pModule, "CRASH_COMPLETE", CC_CRASH_COMPLETE);
}

void TestClient_InitPython(void)
{
	PyObject *pLoadFunc = NULL;

	// Set up our script dir, initialize the environment, load our script
	pyLibSetScriptDir("server/TestClient/python");
	
	if(!pyLibInitialize())
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Could not initialize Python environment!\n");
		return;
	}

	TestClient_RegisterPythonModule();

	if(!(spMainModule = pyLibLoadScript(gTestClientGlobalState.cScriptName, PYLIB_MAIN_MODULE)))
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Could not run main script [%s]!\n", gTestClientGlobalState.cScriptName);
		return;
	}

	pyLibInitVars();
	pLoadFunc = pyLibGetFuncSafe(spMainModule, "OnLoad");

	if(!pLoadFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"%s\" has no OnLoad function - it will be unable to function!\n", gTestClientGlobalState.cScriptName);
		return;
	}

	if(!PyObject_CallObject(pLoadFunc, NULL))
	{
		PyErr_Print();
	}

	return;
}

void TestClient_PythonTick(void)
{
	int i;
	
	for(i = 0; i < eaSize(&sppCallbacks); ++i)
	{
		if(!PyObject_CallObject(sppCallbacks[i], NULL))
		{
			PyErr_Print();
		}
	}
}