#pragma once

#include "UGCProjectCommon.h"

#include "stdtypes.h"
#include "objContainer.h"

#define ASSERT_CONTAINER_DATA_UGC(msg)	if(GetAppGlobalType() != GLOBALTYPE_UGCDATAMANAGER) AssertOrAlert(__FUNCTION__, msg)

void aslUGCDataManager_SendAllPlayableNameSpacesToAllMapManagersAtStartup(void);

void aslUGCDataManager_ServerMonitor_RecordRecentlyChangedPlayableNamespace(UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData);
void aslUGCDataManager_ServerMonitor_RecordMissionRecentlyCompleted(const char *strNameSpace, U32 uDurationInMinutes);

bool aslUGCDataManager_IsPublishDisabled();
