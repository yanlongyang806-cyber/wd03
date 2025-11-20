#ifndef GSL_UGC_TRANSACTIONS_H
#define GSL_UGC_TRANSACTIONS_H

#include "GlobalTypeEnum.h"
#include "objTransactions.h"

typedef struct Entity Entity;
typedef struct InfoForUGCProjectSaveOrPublish InfoForUGCProjectSaveOrPublish;
typedef struct InfoForUGCProjectSaveOrPublish;
typedef struct NOCONST(UGCAccount) NOCONST(UGCAccount);
typedef struct NOCONST(UGCProject) NOCONST(UGCProject);
typedef struct UGCProjectData UGCProjectData;
typedef struct UGCProject UGCProject;

bool gslUGC_UploadAutosave(const char *pNameSpace, char **estrErrorMessage);
bool gslUGC_DoSaveForProject(UGCProject *pProject, UGCProjectData *data, InfoForUGCProjectSaveOrPublish *pInfo, ContainerID *pID, bool bPublish, char **pcOutError, const char* strReason);
bool gslUGC_DoSave(UGCProjectData *data, InfoForUGCProjectSaveOrPublish *pInfo, ContainerID *pID, bool bPublish, char **pcOutError, const char* strReason);
bool gslUGC_DoSaveResourcesToDisk(UGCProjectData *data, char **pstrError);
void gslUGC_DoProjectPublishPostTransaction(Entity *pEntity, InfoForUGCProjectSaveOrPublish *pInfoForPublish);
bool gslUGC_ForkNamespace(UGCProjectData *pData, InfoForUGCProjectSaveOrPublish *pInfoForPublish, bool bUpload);
int gslUGC_RenameNamespaceResources(const char* pDictName, const char* pEditingNameSpace, const char* pPublishNameSpace);

#endif
