#include "luaScriptLib.h"
#include "luaInternals.h"

typedef struct TestClientChatEntry
{
	char		*pChannel;
	char		*pSender;
	char		*pMessage;
} TestClientChatEntry;

typedef struct TestClientNotifyEntry
{
	char		*pName;
	char		*pObject;
	char		*pString;
} TestClientNotifyEntry;

typedef enum TestClientCallbackType TestClientCallbackType;

// Stores info for a generic Lua callback
typedef struct TestClientEventCallback
{
	int						iCallbackHandle;
	int						iCallbackRef;
	TestClientCallbackType	eType;
	F32						fCallbackDelay;
	F32						fLastCallback;

	// Only valid if TC_CALLBACK_NOTIFY
	char					*pchNotifyName;

	bool					bPersistent;
	bool					bRemove;
} TestClientEventCallback;

typedef struct TestClientTicket
{
	int		iID;
	int		iHandle;
} TestClientTicket;

typedef struct TestClientEntityWrapper
{
	EntityRef iRef;
} TestClientEntityWrapper;

typedef struct TestClientObjectWrapper
{
	char key[260];
} TestClientObjectWrapper;

typedef struct TestClientMissionWrapper
{
	char indexName[260];
} TestClientMissionWrapper;

typedef struct TestClientPower TestClientPower;

int TestClient_RegisterEventCallback(TestClientCallbackType eType, int iFuncRef, bool bPersist);
bool TestClient_UnregisterCallback(int iCallbackHandle);

void TestClient_StateMachineTick(void);
void TestClient_CallbackTick(void);

void TestClient_InitCallbacks(LuaContext *pContext);
void TestClient_InitEntity(LuaContext *pContext);
void TestClient_InitMetrics(LuaContext *pContext);
void TestClient_InitMission(LuaContext *pContext);
void TestClient_InitMove(LuaContext *pContext);
void TestClient_InitStateMachine(LuaContext *pContext);
void TestClient_InitTicket(LuaContext *pContext);

#define TestClient_GetEntity(argn) _TestClient_GetEntity(_lua_, argn)
#define TestClient_PushEntity(ref) (_retcount_++, _TestClient_PushEntity(_lua_, ref))
void *_TestClient_GetEntity(lua_State *L, int argn);
void _TestClient_PushEntity(lua_State *L, TestClientEntityWrapper *ent);

#define TestClient_GetObject(argn) _TestClient_GetObject(_lua_, argn)
#define TestClient_PushObject(ref) (_retcount_++, _TestClient_PushObject(_lua_, ref))
void *_TestClient_GetObject(lua_State *L, int argn);
void _TestClient_PushObject(lua_State *L, TestClientObjectWrapper *obj);

#define TestClient_GetPower(argn) _TestClient_GetPower(_lua_, argn)
#define TestClient_PushPower(ref) (_retcount_++, _TestClient_PushPower(_lua_, ref))
void *_TestClient_GetPower(lua_State *L, int argn);
void _TestClient_PushPower(lua_State *L, TestClientPower *power);

#define TestClient_GetMission(argn) _TestClient_GetMission(_lua_, argn)
#define TestClient_PushMission(ref) (_retcount_++, _TestClient_PushMission(_lua_, ref))
void *_TestClient_GetMission(lua_State *L, int argn);
void _TestClient_PushMission(lua_State *L, TestClientMissionWrapper *mission);