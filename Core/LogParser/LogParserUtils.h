#pragma once

extern bool sbLogParserGotShardNames;
static __forceinline bool LogParser_ClusterLevel_GotShardNames(void)
{
	return sbLogParserGotShardNames;
}

char *LogParser_GetClusterNameFromID(int iID);


void LogParser_AddClusterStuffToStandAloneCommandLine(char **ppCmdLine);