#pragma once
typedef struct ChatChannelInfo ChatChannelInfo;

void ClientChat_ClearSelectedAdminChannel(void);
ChatChannelInfo *ClientChat_GetCachedChannelInfo(const char *pchChannelName);