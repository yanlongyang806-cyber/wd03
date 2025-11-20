#pragma once

typedef struct NetLink NetLink;

void XMPP_SendGlobalChatQuery(NetLink *link, const char *pCommand, const char *pParamFormat, ... );