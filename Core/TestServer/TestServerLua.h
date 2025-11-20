#pragma once

#include "WorkerThread.h"

typedef struct TestServerStatus TestServerStatus;

AUTO_ENUM;
typedef enum TestScriptStatus
{
	TSS_Queued,
	TSS_Active,

	// Completed pending removal
	TSS_Interrupted,
	TSS_Completed,
} TestScriptStatus;

AUTO_STRUCT;
typedef struct TestScript
{
	char				*pchScript; AST(ESTRING)
	bool				bRaw;
	TestScriptStatus	eState;
} TestScript;

enum {
	WT_CMD_EXECUTE_SCRIPT = WT_CMD_USER_START,
	WT_CMD_SCRIPT_COMPLETE,
	WT_CMD_XPATH_REQUEST,
};

void TestServer_VerifyScriptRaw(const char *script);
void TestServer_VerifyScripts(void);
void TestServerStatus_LuaPopulate(TestServerStatus *data);

void TestServer_RunScript(const char *fn);
void TestServer_RunScriptNow(const char *fn);
void TestServer_RunScriptRaw(const char *script);
void TestServer_RunScriptRawNow(const char *script);

void TestServer_StartLuaThread(void);
void TestServer_MonitorLuaThread(void);
void TestServer_InterruptScript(void);
void TestServer_CancelScript(int iIndex);
void TestServer_CancelAllScripts(void);