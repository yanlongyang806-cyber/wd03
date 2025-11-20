#pragma once

#define TC_MAX_CHAT_QUEUE 1024
#define TC_MAX_NOTIFY_QUEUE 1024

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

void TestClient_GetQueuedChat(TestClientChatEntry ***pppOutChat);
void TestClient_GetQueuedNotify(TestClientNotifyEntry ***pppOutChat);

void TestClient_InitScripting(void);
void TestClient_ScriptingTick(void);
void TestClient_ScriptExecute(const char *script);
void TestClient_ClientScriptExecute(const char *script);

F32 TestClient_GetScriptTime(void);
