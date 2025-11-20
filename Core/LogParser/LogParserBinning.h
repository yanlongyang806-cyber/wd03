#pragma once

typedef struct ParsedLog ParsedLog;

void LogParserBinning_Reset(char *pDescriptionString);
void LogParserBinning_AddLog(ParsedLog *pLog);
void LogParserBinning_Close(void);

void LogParserBinning_GetCurDescriptionString(char **ppEString);
void LogParserBinning_ParserAllLogsFromBin(void);