typedef struct UGCSearchResult UGCSearchResult;
typedef struct UGCPatchInfo UGCPatchInfo;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectSeries UGCProjectSeries;
typedef struct UGCProjectSearchInfo UGCProjectSearchInfo;

typedef U32 ContainerID;

bool UGCExportImport_InitPython(void);
bool UGCExportImport_ProxyInit(const char *machine, const char *username);
bool UGCExportImport_Version();
char *UGCExportImport_SearchInit(bool includeSaved, bool includePublished, UGCProjectSearchInfo *pUGCProjectSearchInfo);
UGCSearchResult *UGCExportImport_SearchNext(const char *searchKey);
UGCPatchInfo *UGCExportImport_GetUGCPatchInfo();
UGCProject *UGCExportImport_GetUGCProjectContainer(ContainerID uUGCProjectID);
UGCProjectSeries *UGCExportImport_GetUGCProjectSeriesContainer(ContainerID uUGCProjectSeriesID);
bool UGCExportImport_DeleteAllUGC(const char *strComment);
char *UGCExportImport_ImportUGCProjectContainerAndData(UGCProject *pUGCProject, const char *estrUGCProjectDataPublished, const char *estrUGCProjectDataSavedconst,
	const char *strPreviousShard, const char *strComment, bool forceDelete);
char *UGCExportImport_ImportUGCProjectSeriesContainer(UGCProjectSeries *pUGCProjectSeries, const char *strPreviousShard, const char *strComment, bool forceDelete);

int UGCExportImport_InputTest(const char *prompt, const char *success);

char *UGCExportImport_DecompressMemoryToText(const char *data, U32 size, int* outputSize);
