#pragma once

#define CHATSERVER_CONNECT_TIMEOUT 30
#define CHATSERVER_RECONNECT_TIME 60 // starts counting from when it first tries to connect, not after timeout
#define CHATSERVER_VERSION_RECONNECT_TIME 300 // time between reconnect attempts when there was a version mismatch

typedef struct NetComm NetComm;
typedef struct NetLink NetLink;

const char *getGlobalChatServer(void);
NetComm *getCommToGlobalChat(void);
bool sendCommandToGlobalChat(NetLink *link, const char *pCommandString);