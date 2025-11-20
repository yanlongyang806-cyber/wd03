#pragma once

void DoAutoStartup(void);
void AutoStartup_AddDependency(char *pTask, char *pTaskItDependsOn);
void AutoStartup_RemoveDependency(char *pTask, char *pTaskItShouldNotDependOn);
void AutoStartup_RemoveAllDependenciesOn(char *pTaskNothingShouldDependOn);
void AutoStartup_SetTaskIsOn(char *pTask, int iIsOn);