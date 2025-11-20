#define TESTCLIENT_CRASHBEGAN_EVENT "TestClientCrashBegan"
#define TESTCLIENT_CRASHCOMPLETED_EVENT "TestClientCrashCompleted"

typedef struct LuaContext LuaContext;
typedef struct TestClientStateUpdate TestClientStateUpdate;
typedef struct TestClientChatEntry TestClientChatEntry;
typedef struct TestClientNotifyEntry TestClientNotifyEntry;
typedef U32 ContainerID;

typedef struct TestClientGlobalState
{
	// Exe stuff
	ContainerID				iID;
	char					*pcStatus;

	int						iFPS;
	bool					bNoShardMode;
	bool					bRunning;

	// Script stuff
	char					cScriptName[260];
	TestClientChatEntry		**ppChat;
	TestClientNotifyEntry	**ppNotify;

	// Game stuff
	ContainerID				iOwnerID;
	ContainerID				iEntityID;
	TestClientStateUpdate	*pLatestState;
} TestClientGlobalState;

extern TestClientGlobalState gTestClientGlobalState;
extern bool gbTestClientRunning;