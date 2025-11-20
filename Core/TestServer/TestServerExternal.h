typedef struct NetLink NetLink;
typedef struct Packet Packet;

typedef void TestServer_ExternalLink_MessageCB(Packet *pkt, int cmd, NetLink *link, int index, void *user_data);
typedef void TestServer_ExternalLink_DisconnectCB(NetLink *link, int index, void *user_data);

void TestServer_InitExternal(void);
Packet *TestServer_GetDestinationPacket(NetLink *link, int index, int cmd);

void *TestServer_GetLinkUserData(NetLink *link, int index);
TestServer_ExternalLink_MessageCB *TestServer_GetLinkMessageCallback(NetLink *link, int index);
TestServer_ExternalLink_DisconnectCB *TestServer_GetLinkDisconnectCallback(NetLink *link, int index);

void TestServer_SetLinkUserData(NetLink *link, int index, void *user_data);
void TestServer_SetLinkMessageCallback(NetLink *link, int index, TestServer_ExternalLink_MessageCB *pCallback);
void TestServer_SetLinkDisconnectCallback(NetLink *link, int index, TestServer_ExternalLink_DisconnectCB *pCallback);