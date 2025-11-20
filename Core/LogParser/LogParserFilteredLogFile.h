#pragma once

//the "filtered log file" is a file which contains all the logs found by a particular filtered search, in
//raw log form. It's optionally created every time a particular query is run.
void InitFilteredLogFile(bool bCompressed);
void AbortFilteredLogFile(void);
void CloseFilteredLogFile(void);
void AddLogToFilteredLogFile(ParsedLog *pLog);
void FilteredLogFile_InitSystem(void);
const char *FilteredLogFile_GetRecentFileName(void);
const char *FilteredLogFile_GetDownloadPath(void);

// In memory mode, filter to a buffer instead of to a file.
void InitFilteredLogMemory(void);
const char *const *GetFilteredLogMemory(void);
