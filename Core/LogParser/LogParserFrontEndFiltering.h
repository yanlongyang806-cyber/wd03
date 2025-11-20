#pragma once

typedef struct ParsedLog ParsedLog;
typedef struct LogParserStandAloneOptions LogParserStandAloneOptions;

extern bool gbDoFrontEndFiltering;

void FrontEndFiltering_AddLog(ParsedLog *pLog);
void FrontEndFiltering_Done(void);
void FrontEndFiltering_Tick(void);

void FrontEndFiltering_Start(char *pOutDir, int iSecondsPerFile, char *pStandAloneOptions_SuperEsc);