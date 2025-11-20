// Header file to include if you want to talk to the Test Server
#pragma once

extern bool gbIsTestServerHostSet;
extern char gTestServerHost[CRYPTIC_MAX_PATH];
extern int gTestServerPort;

// IF YOU MODIFY THIS ENUM, PLEASE NOTIFY VINAY
// OR PLEASE ADD A CORRESPONDING ENTRY IN THE TestServerViewItemType ENUM
AUTO_ENUM;
typedef enum TestServerGlobalType
{
	TSG_Unset = 0,
	TSG_Single,
	TSG_Array,
	TSG_Metric,
	TSG_Expression,
} TestServerGlobalType;

AUTO_ENUM;
typedef enum TestServerGlobalValueType
{
	TSGV_Unset = 0,
	TSGV_Integer,
	TSGV_Boolean,
	TSGV_Float,
	TSGV_String,
	TSGV_Password,
	TSGV_Global,
} TestServerGlobalValueType;

typedef struct TestServerGlobal TestServerGlobal;

AUTO_STRUCT;
typedef struct TestServerGlobalReference
{
	const char					*pcScope; AST(POOL_STRING)
	const char					*pcName; AST(POOL_STRING)

	bool						bSet;
	TestServerGlobal			*pGlobal;
} TestServerGlobalReference;

AUTO_STRUCT;
typedef struct TestServerGlobalValue
{
	TestServerGlobalValueType	eType;
	char						*pcLabel; AST(ESTRING STRUCTPARAM)

	int							iIntVal;
	bool						bBoolVal;
	float						fFloatVal;
	char						*pcStringVal; AST(ESTRING)
	char						*pcPasswordVal; NO_AST
	TestServerGlobalReference	*pRefVal;
} TestServerGlobalValue;

AUTO_STRUCT;
typedef struct TestServerGlobal
{
	const char				*pcScope; AST(POOL_STRING)
	const char				*pcName; AST(POOL_STRING)
	TestServerGlobalType	eType;
	bool					bPersist;

	TestServerGlobalValue	**ppValues; AST(NAME(Value))
} TestServerGlobal;

typedef struct NetLink NetLink;
typedef struct Packet Packet;

bool CheckTestServerConnection(void);

typedef void (*TestServerGlobalCallback)(TestServerGlobal *pGlobal);
typedef void PacketCallback(Packet *pkt, int cmd, NetLink *link, void *user_data);
typedef void LinkCallback(NetLink* link, void *user_data);

void RegisterTestServerGlobalCallback(const char *pcScope, const char *pcName, TestServerGlobalCallback pCallback);
void SetTestServerMessageHandler(PacketCallback *pCallback);
void SetTestServerDisconnectHandler(LinkCallback *pCallback);

void RequestGlobalFromTestServer(const char *pcScope, const char *pcName);
void ClearGlobalOnTestServer(const char *pcScope, const char *pcName);

Packet *GetPacketToSendToTestServer(int cmd);

void PushMetricToTestServer(const char *pcScope, const char *pcName, float val, bool bPersist);
void SetGlobalOnTestServer_Integer(const char *pcScope, const char *pcName, int val, bool bPersist);
void SetGlobalOnTestServer_Boolean(const char *pcScope, const char *pcName, bool val, bool bPersist);
void SetGlobalOnTestServer_Float(const char *pcScope, const char *pcName, float val, bool bPersist);
void SetGlobalOnTestServer_String(const char *pcScope, const char *pcName, const char *val, bool bPersist);
void SetGlobalOnTestServer_Password(const char *pcScope, const char *pcName, const char *val, bool bPersist);