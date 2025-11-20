#pragma once

typedef struct NetLink NetLink;

NetLink *GetXmppGlobalChatLink(void);

void XMPPServer_Tick(F32 elapsed);