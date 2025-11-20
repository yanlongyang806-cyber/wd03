#pragma once


typedef struct DynamicPatchInfo DynamicPatchInfo;

typedef enum enumFillInPatchInfoFlags
{
	PATCHINFO_FOR_UGC_PLAYING = 1 << 0,
} enumFillInPatchInfoFlags;

void aslFillInPatchInfoForUGC(DynamicPatchInfo *pPatchInfo, enumFillInPatchInfoFlags eFlags);

//returns true on success
bool aslFillInPatchInfo(DynamicPatchInfo *pPatchInfo, const char *pNameSpace, enumFillInPatchInfoFlags eFlags);

//checks whether above will succeed
bool aslCheckNamespaceForPatchInfoFillIn(const char *pNameSpace);


extern bool gbUseShardNameInPatchProjectNames;

//these relate to querying the dynamic patch server for a list of namespaces
bool aslBeginBackgroundThreadNamespaceListQuery(const char *namespace_prefix);
bool aslGetNamespaceListDone(char ***pppNames, U32 **ppModTimes);
void aslNamespaceListCleanup(bool bDontCleanupArrays);
void aslAddNamespaceToBeDeletedNextQuery(const char *pName);