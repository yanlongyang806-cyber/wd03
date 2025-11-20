#include "AutoStartupSupport.h"
#include "MetaTask.h"
#include "GlobalTypes.h"
#include "file.h"
#include "utils.h"

void DoAutoStartup(void)
{
	MetaTask_Register("startup");
	if (!isProductionMode())
	{
		char fileName[MAX_PATH];
		sprintf(fileName, "%s/AutoStartup/%s_AutoStartup.c", fileTempDir(), GlobalTypeToName(GetAppGlobalType()));
		mkdirtree(fileName);
		MetaTask_WriteOutFunctionCalls("startup", fileName);
		sprintf(fileName, "%s/AutoStartup/%s_graph.txt", fileTempDir(), GlobalTypeToName(GetAppGlobalType()));
		MetaTask_WriteOutGraphFile("startup", fileName);
	}

	MetaTask_DoMetaTask("startup");
}

void AutoStartup_AddDependency(char *pTask, char *pTaskItDependsOn)
{
	MetaTask_AddDependencies("startup", pTask, STACK_SPRINTF("AFTER %s", pTaskItDependsOn));
}

void AutoStartup_RemoveDependency(char *pTask, char *pTaskItShouldNotDependOn)
{
	MetaTask_RemoveDependencies("startup", pTask, pTaskItShouldNotDependOn);
}

void AutoStartup_RemoveAllDependenciesOn(char *pTaskNothingShouldDependOn)
{
	MetaTask_RemoveAllDependenciesOn("startup", pTaskNothingShouldDependOn);
}

AUTO_COMMAND ACMD_NAME(SetAutoStartup);
void AutoStartup_SetTaskIsOn(char *pTask, int iIsOn)
{
	MetaTask_SetTaskStartsOn("startup", pTask, (bool)iIsOn);
}

