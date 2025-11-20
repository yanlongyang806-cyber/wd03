#pragma once

#include "HttpXPathSupport.h"
#include "GlobalTypeEnum.h"

void BeginMCPHttpMonitoring(void);
void MCPHttpMonitoringUpdate(void);
void handle_MCPHttpMonitoringProcessXpath(GlobalType eServingType, ContainerID iServingID, int iRequestID, StructInfoForHttpXpath *pStructInfo);
void HandleMonitoringCommandReturn(Packet *pPak);
void HandleJpegReturn(Packet *pPak);
void HandleFileServingReturn(Packet *pak);

U32 **MCPHttpMonitoringGetMonitoredPorts(void);