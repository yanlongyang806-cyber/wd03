#pragma once

void aslMapManagerVirtualShards_StartNormalOperation(void);
void aslMapManagerVirtualShards_NormalOperation(void);

int SetVirtualShardEnabled(const char *pcName, bool bEnabled);

int EnableUGCVirtualShard(void);
int DisableUGCVirtualShard(void);
