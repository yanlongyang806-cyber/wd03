/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "itemCommon.h"
#include "ResourceManager.h"

typedef struct Item Item;
typedef struct Entity Entity;
typedef struct ParseTable ParseTable;
typedef struct CmdSlowReturnForServerMonitorInfo CmdSlowReturnForServerMonitorInfo;
typedef struct CmdContext CmdContext;

void WebRequestSlow_BuildXMLResponseString(char **responseString, ParseTable *tpi, void *struct_mem);
void WebRequestSlow_BuildXMLResponseStringWithType(char **responseString, char *type, char *val);
void WebRequestSlow_SendXMLRPCReturn(bool success, CmdSlowReturnForServerMonitorInfo *slowReturnInfo);
CmdSlowReturnForServerMonitorInfo *WebRequestSlow_SetupSlowReturn(CmdContext *pContext);
void AccountSharedBankReceived_CB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, Entity *pEnt, void *pUserData);