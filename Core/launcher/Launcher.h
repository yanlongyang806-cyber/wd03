#pragma once

typedef struct ServerLaunchRequest ServerLaunchRequest;

void StartProcessFromRequest(ServerLaunchRequest *pRequest);

char *ExtractShortExeName(char *pExeName_In);