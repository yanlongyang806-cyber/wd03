#pragma once

void LogParserLaunching_PeriodicUpdate(void);
U32 LogParserLaunching_LaunchOne(int iTimeOut, const char *pExtraArgs, int iForcedPortNum, int iUID);
int FillStandAloneList(STRING_EARRAY *pArray);
bool LogParserLaunching_IsActive(void);
void UpdateLinkToLiveLogParser();
void InitStandAloneListening();
void LogParserShutdownStandAlones();
void LogParserLaunching_KillByUID(int iUID);