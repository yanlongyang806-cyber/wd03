#pragma once

void Controller_BeginPCLStatusMonitoring(void);

//names for different patching types
#define CONTROLLER_PCL_STARTUP "Normal_Startup"
#define CONTROLLER_PCL_PRIMING "Priming"
#define CONTROLLER_GROUP_PATCHING "Group_Patching"

char *Controller_GetPCLStatusMonitoringCmdLine(char *pMachineName, char *pTaskName);
void Controller_AddPCLStatusMonitoringTask(char *pMachineName, char *pTaskName);
void Controller_PCLStatusMonitoringTick(void);

char *Controller_GetPCLStatusMonitoringDescriptiveStatusString(char *pMachineName, char *pTaskName);

bool Controller_CheckTaskFailedByName(char *pMachineName, char *pTaskName);

void Controller_ResetPCLStatusMonitoringForTask(char *pTaskName);