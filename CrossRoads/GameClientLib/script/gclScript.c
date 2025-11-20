#include "gclScript.h"
#include "gclScriptAPI.h"
#include "pyLib.h"

#include "GameClientLib.h"
#include "gclBaseStates.h"
#include "gclLogin.h"
#include "gclSendToServer.h"

#include "cmdparse.h"
#include "earray.h"
#include "fileLoader.h"
#include "GlobalStateMachine.h"
#include "logging.h"
#include "MapDescription.h"
#include "pyLibStruct.h"
#include "TestClientCommon.h"
#include "textparser.h"
#include "textparserUtils.h"
#include "timing.h"
#include "timing_profiler.h"
#include "tokenstore.h"
#include "utils.h"

#include "TestClientCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping("c:\\src\\libs\\pythonlib\\pylib.c", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("c:\\src\\libs\\pythonlib\\pylibstruct.c", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static char sScriptName[CRYPTIC_MAX_PATH] = "Default";
AUTO_CMD_STRING(sScriptName, GCLScriptName) ACMD_CMDLINE;

static int siGCLScriptID = 1;
AUTO_CMD_INT(siGCLScriptID, GCLScriptID) ACMD_CMDLINE;

static PyObject *spMainModule = NULL;
static int siScriptTimer = 0;

// Arrays of stuff
static PyObject **sppCallbacks = NULL;
static PyObject **sppQueuedChat = NULL;
static PyObject **sppQueuedNotify = NULL;

static PY_FUNC(gclScript_Log)
{
	const char *pcLogStr = NULL;

	PY_PARSE_ARGS("s", &pcLogStr);
	log_printf(LOG_TC_TESTCLIENT, "%s", pcLogStr);
	return PyLib_IncNone;
}

static PY_FUNC(gclScript_Exit)
{
	PY_PARSE_ARGS("");
	GSM_Quit("gcl.exit called");
	return PyLib_IncNone;
}

static PY_FUNC(gclScript_Execute)
{
	const char *pcCommand = NULL;
	char *pcReturnStr = NULL;
	PyObject *pRetObject = NULL;

	PERFINFO_AUTO_START_FUNC();
	PY_PARSE_ARGS_PERF_STOP("s", &pcCommand);
	globCmdParseAndReturnWithFlagsAndOverrideAccessLevel(pcCommand, &pcReturnStr, CMD_CONTEXT_FLAG_TREAT_PRINTVARS_AS_SUCCESS, ACCESS_DEBUG, CMD_CONTEXT_HOWCALLED_UNSPECIFIED);
	pRetObject = Py_BuildValue("s", pcReturnStr);
	estrDestroy(&pcReturnStr);
	PERFINFO_AUTO_STOP();

	return pRetObject;
}

static PY_FUNC(gclScript_RegisterTick)
{
	PyObject *pCallback = NULL;

	PY_PARSE_ARGS("O", &pCallback);

	if(!PyCallable_Check(pCallback))
	{
		PyErr_BadArgument();
		return NULL;
	}

	eaPush(&sppCallbacks, pCallback);
	return PyLib_IncNone;
}

static PY_FUNC(gclScript_GetScript)
{
	PY_PARSE_ARGS("");
	return Py_BuildValue("s", sScriptName);
}

static F32 gclScript_ElapsedTime(void)
{
	return timerElapsed(siScriptTimer);
}

static PY_FUNC(gclScript_GetTime)
{
	PY_PARSE_ARGS("");
	return Py_BuildValue("f", gclScript_ElapsedTime());
}

static PY_FUNC(gclScript_GetID)
{
	PY_PARSE_ARGS("");
	return Py_BuildValue("i", siGCLScriptID);
}

static PY_FUNC(gclScript_GetFPS)
{
	PY_PARSE_ARGS("");
	return Py_BuildValue("f", 2.0f);
}

static PY_FUNC(gclScript_GetFSMState)
{
	char *pcStateStr = NULL;
	PyObject *pRetObject = NULL;

	PERFINFO_AUTO_START_FUNC();
	PY_PARSE_ARGS_PERF_STOP("");
	GSM_PutFullStateStackIntoEString(&pcStateStr);
	pRetObject = Py_BuildValue("s", pcStateStr);
	estrDestroy(&pcStateStr);
	PERFINFO_AUTO_STOP();

	return pRetObject;
}

extern TestClientStateUpdate gGCLTestClientState;

static PY_FUNC(gclScript_GetGameState)
{
	PyObject *pState = NULL;

	PERFINFO_AUTO_START_FUNC();
	PY_PARSE_ARGS_PERF_STOP("");
	pState = pyLibSerializeStruct(&gGCLTestClientState, parse_TestClientStateUpdate);
	PERFINFO_AUTO_STOP();

	return pState;
}

extern PossibleMapChoices *g_pGameServerChoices;
static PY_FUNC(gclScript_GetMapChoices)
{
	PyObject *pChoices = NULL;

	PERFINFO_AUTO_START_FUNC();
	PY_PARSE_ARGS_PERF_STOP("");

	if(!GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_GAMESERVER) || !g_pGameServerChoices)
	{
		PERFINFO_AUTO_STOP();
		return PyLib_IncNone;
	}

	pChoices = pyLibSerializeStruct(g_pGameServerChoices, parse_PossibleMapChoices);
	PERFINFO_AUTO_STOP();

	return pChoices;
}

static PY_FUNC(gclScript_PendingPatches)
{
	PY_PARSE_ARGS("");
	return Py_BuildValue("i", fileLoaderPatchesPending());
}

static PY_FUNC(gclScript_ChooseMap)
{
	int iChoice = 0;

	PY_PARSE_ARGS("i", &iChoice);
	if(GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_GAMESERVER) && g_pGameServerChoices)
		gclLoginChooseMap(g_pGameServerChoices->ppChoices[iChoice]);
	return PyLib_IncNone;
}

static PY_FUNC(gclScript_Status)
{
	const char *pcStatus = NULL;

	PY_PARSE_ARGS("s", &pcStatus);
	SendCommandStringToTestClientf("Status %s", pcStatus);
	return PyLib_IncNone;
}

static PY_FUNC(gclScript_QueuedChat)
{
	PyObject *pQueue = NULL;
	PyObject *pEntry = NULL;

	PERFINFO_AUTO_START_FUNC();
	PY_PARSE_ARGS_PERF_STOP("");

	pQueue = PyList_New(0);

	while(eaSize(&sppQueuedChat) > 0)
	{
		pEntry = eaPop(&sppQueuedChat);
		PyList_Insert(pQueue, 0, pEntry);
		Py_XDECREF(pEntry);
	}

	PERFINFO_AUTO_STOP();
	return pQueue;
}

static PY_FUNC(gclScript_QueuedNotify)
{
	PyObject *pQueue = NULL;
	PyObject *pEntry = NULL;

	PERFINFO_AUTO_START_FUNC();
	PY_PARSE_ARGS_PERF_STOP("");

	pQueue = PyList_New(0);

	while(eaSize(&sppQueuedNotify) > 0)
	{
		pEntry = eaPop(&sppQueuedNotify);
		PyList_Insert(pQueue, 0, pEntry);
		Py_XDECREF(pEntry);
	}

	PERFINFO_AUTO_STOP();
	return pQueue;
}

static PY_FUNC(gclScript_KillLink)
{
	PY_PARSE_ARGS("");
	gclServerForceDisconnect("gclScript_KillLink");
	return PyLib_IncNone;
}

static PyMethodDef gclScriptMethods[] = {
	PY_FUNC_DEF(gclScript_Log, "log", "Send a string to the Log Server using the TC_TESTCLIENT log type."),
	PY_FUNC_DEF(gclScript_Exit, "exit", "Finish execution of the Game Client and exit."),
	PY_FUNC_DEF(gclScript_Execute, "execute", "Execute an auto command."),
	PY_FUNC_DEF(gclScript_RegisterTick, "register_tick", "Register a tick function to be called every frame."),
	PY_FUNC_DEF(gclScript_GetScript, "get_script", "Get the name of the script passed to the Game Client."),
	PY_FUNC_DEF(gclScript_GetID, "get_id", "Get the ID of the Test Client."),
	PY_FUNC_DEF(gclScript_GetTime, "get_time", "Get the time since the scripting environment started."),
	PY_FUNC_DEF(gclScript_GetFPS, "get_fps", "Get the framerate of the Test Client."),
	PY_FUNC_DEF(gclScript_GetFSMState, "get_fsm_state", "Get the current FSM of the Game Client."),
	PY_FUNC_DEF(gclScript_GetGameState, "get_game_state", "Get the current state of the game world."),
	PY_FUNC_DEF(gclScript_GetMapChoices, "get_map_choices", "Get the list of possible map choices."),
	PY_FUNC_DEF(gclScript_PendingPatches, "pending_patches", "Get the number of pending patches."),
	PY_FUNC_DEF(gclScript_ChooseMap, "choose_map", "Choose a map by index in the choice array."),
	PY_FUNC_DEF(gclScript_Status, "status", "Provide an update to the Test Client on the status of the script."),
	PY_FUNC_DEF(gclScript_QueuedChat, "get_queued_chat", "Get the list of queued chat entries."),
	PY_FUNC_DEF(gclScript_QueuedNotify, "get_queued_notify", "Get the list of queued notifications."),
	PY_FUNC_DEF(gclScript_KillLink, "kill_link", "Forcibly kill the link to the Game Server."),
	PY_FUNC_TERM,
};

/*
static PyMethodDef gclScriptEntityMethods[] = {
	PY_FUNC_DEF(gclScript_GetPlayerRef, "player", "Get a reference to the player entity."),
	PY_FUNC_DEF(gclScript_GetEntityName, "name", "Get the name of a specified entity."),
	PY_FUNC_DEF(gclScript_GetEntityID, "id", "Get the container ID of a specified entity."),
	PY_FUNC_DEF(gclScript_GetEntityHP, "hp", "Get the current HP of a specified entity."),
	PY_FUNC_DEF(gclScript_GetEntityMaxHP, "max_hp", "Get the maximum HP of a specified entity."),
	PY_FUNC_DEF(gclScript_GetEntityLevel, "level", "Get the level of a specified entity."),
	PY_FUNC_DEF(gclScript_GetEntityTarget, "target", "Get the specified entity's target."),
	PY_FUNC_DEF(gclScript_GetEntityShields, "shields", "Get an array indicating the specified entity's current shield power levels."),
	PY_FUNC_DEF(gclScript_GetEntityPos, "pos", "Get an array indicating the specified entity's current position."),
	PY_FUNC_DEF(gclScript_GetEntityPYR, "pyr", "Get an array indicating the specified entity's current PYR facing."),
	PY_FUNC_DEF(gclScript_GetEntityDistance, "distance", "Get the distance of the specified entity from the player."),
	PY_FUNC_DEF(gclScript_GetEntityNearbyFriends, "friends", "Get an array of friendly entities near the specified entity."),
	PY_FUNC_DEF(gclScript_GetEntityNearbyHostiles, "hostiles", "Get an array of hostile entities near the specified entity."),
	PY_FUNC_DEF(gclScript_GetEntityNearbyObjects, "objects", "Get an array of objects near the specified entity."),
	PY_FUNC_DEF(gclScript_IsEntityValid, "valid", "Check that the specified entity is valid."),
	PY_FUNC_DEF(gclScript_IsEntityDead, "dead", "Check that the specified entity is dead."),
	PY_FUNC_DEF(gclScript_IsEntityHostile, "hostile", "Check that the specified entity is hostile to the player."),
	PY_FUNC_DEF(gclScript_IsEntityCasting, "casting", "Check that the specified entity is currently casting."),
	PY_FUNC_DEF(gclScript_DoesEntityHaveMission, "has_mission", "Check that the specified entity offers a mission that the player can accept."),
	PY_FUNC_DEF(gclScript_DoesEntityHaveCompletedMission, "has_completed_mission", "Check that the specified entity can complete a mission the player has."),
	PY_FUNC_DEF(gclScript_IsEntityImportant, "important", "Check that the specified entity has important information for the player."),
	PY_FUNC_TERM,
};*/

static void gclScript_RegisterModules(void)
{
	PyObject *pModule = Py_InitModule("gcl", gclScriptMethods);
	//PyModule_AddObject(pModule, "entity", Py_InitModule("entity", gclScriptEntityMethods));
}

void gclScript_Init(void)
{
	PyObject *pLoadFunc = NULL;

	// Set up our script dir, initialize the environment, load our script
	pyLibSetScriptDir("gclScript");

	if(!pyLibInitialize())
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Could not initialize Python environment!\n");
		return;
	}

	gclScript_RegisterModules();
	siScriptTimer = timerAlloc();
	timerStart(siScriptTimer);

	if(!(spMainModule = pyLibLoadScript(sScriptName, PYLIB_MAIN_MODULE)))
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Could not run main script [%s]!\n", sScriptName);
		return;
	}

	pyLibInitVars();
	pLoadFunc = pyLibGetFuncSafe(spMainModule, "OnLoad");

	if(!pLoadFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"%s\" has no OnLoad function - it will be unable to function!\n", sScriptName);
		return;
	}

	if(!PyObject_CallObject(pLoadFunc, NULL))
	{
		PyErr_Print();
	}

	return;
}

void gclScript_Tick(void)
{
	int i;

	if(!spMainModule)
		return;

	for(i = 0; i < eaSize(&sppCallbacks); ++i)
	{
		if(!PyObject_CallObject(sppCallbacks[i], NULL))
		{
			PyErr_Print();
		}
	}
}

AUTO_COMMAND;
void gclScript_Run(ACMD_SENTENCE pcScript)
{
	if(!spMainModule) return;
	pyLibExecute(pcScript);
}

void gclScript_QueueChat(const char *pcChannel, const char *pcSender, const char *pcMessage)
{
	PyObject *pEntry = NULL;

	if(!spMainModule) return;
	pEntry = Py_BuildValue("sss", pcChannel, pcSender, pcMessage);
	eaPush(&sppQueuedChat, pEntry);
}

void gclScript_QueueNotify(const char *pcName, const char *pcObject, const char *pcString)
{
	PyObject *pEntry = NULL;

	if(!spMainModule) return;
	pEntry = Py_BuildValue("sss", pcName, pcObject, pcString);
	eaPush(&sppQueuedNotify, pEntry);
}
