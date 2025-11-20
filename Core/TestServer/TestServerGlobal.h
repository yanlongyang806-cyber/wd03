#pragma once

typedef enum TestServerGlobalType TestServerGlobalType;
typedef enum TestServerGlobalValueType TestServerGlobalValueType;

void TestServer_ClearGlobal(const char *pcScope, const char *pcName);
void TestServer_ClearGlobalValue(const char *pcScope, const char *pcName, int pos);

TestServerGlobalType TestServer_GetGlobalType(const char *pcScope, const char *pcName);
void TestServer_SetGlobalType(const char *pcScope, const char *pcName, TestServerGlobalType eType);
bool TestServer_IsGlobalPersisted(const char *pcScope, const char *pcName);
void TestServer_PersistGlobal(const char *pcScope, const char *pcName, bool bPersist);
const char *TestServer_GetGlobalValueLabel(const char *pcScope, const char *pcName, int pos);
void TestServer_SetGlobalValueLabel(const char *pcScope, const char *pcName, int pos, const char *pcLabel);
TestServerGlobalValueType TestServer_GetGlobalValueType(const char *pcScope, const char *pcName, int pos);
int TestServer_GetGlobalValueCount(const char *pcScope, const char *pcName);

int TestServer_GetGlobal_Integer(const char *pcScope, const char *pcName, int pos);
bool TestServer_GetGlobal_Boolean(const char *pcScope, const char *pcName, int pos);
float TestServer_GetGlobal_Float(const char *pcScope, const char *pcName, int pos);
const char *TestServer_GetGlobal_String(const char *pcScope, const char *pcName, int pos);
const char *TestServer_GetGlobal_Password(const char *pcScope, const char *pcName, int pos);
const char *TestServer_GetGlobal_RefScope(const char *pcScope, const char *pcName, int pos);
const char *TestServer_GetGlobal_RefName(const char *pcScope, const char *pcName, int pos);

void TestServer_SetGlobal_Integer(const char *pcScope, const char *pcName, int pos, int val);
void TestServer_SetGlobal_Boolean(const char *pcScope, const char *pcName, int pos, bool val);
void TestServer_SetGlobal_Float(const char *pcScope, const char *pcName, int pos, float val);
void TestServer_SetGlobal_String(const char *pcScope, const char *pcName, int pos, const char *val);
void TestServer_SetGlobal_Password(const char *pcScope, const char *pcName, int pos, const char *val);
void TestServer_SetGlobal_Ref(const char *pcScope, const char *pcName, int pos, const char *scope, const char *name);

void TestServer_InsertGlobal_Integer(const char *pcScope, const char *pcName, int pos, int val);
void TestServer_InsertGlobal_Boolean(const char *pcScope, const char *pcName, int pos, bool val);
void TestServer_InsertGlobal_Float(const char *pcScope, const char *pcName, int pos, float val);
void TestServer_InsertGlobal_String(const char *pcScope, const char *pcName, int pos, const char *val);
void TestServer_InsertGlobal_Password(const char *pcScope, const char *pcName, int pos, const char *val);
void TestServer_InsertGlobal_Ref(const char *pcScope, const char *pcName, int pos, const char *scope, const char *name);

void TestServer_InitGlobals(void);
void TestServer_GlobalAtomicBegin(void);
void TestServer_GlobalAtomicEnd(void);

typedef struct Packet Packet;
void TestServer_SendGlobal(const char *pcScope, const char *pcName, Packet *pkt);

typedef struct TestServerView TestServerView;
TestServerView *TestServer_AllGlobalsToView(bool bExpand);
TestServerView *TestServer_ScopedGlobalsToView(const char *pcScope, bool bExpand);
TestServerView *TestServer_GlobalToView(const char *pcScope, const char *pcName, bool bExpand);
TestServerView *TestServer_PersistedGlobalsToView(bool bExpand);

void TestServer_ReadInGlobals(void);
void TestServer_WriteOutGlobals(void);