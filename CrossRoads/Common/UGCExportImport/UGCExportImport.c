#include "UGCExportImport.h"

#include "resource_ugcexportimport.h"

#include "pyLib.h"
#include "utils.h"
#include "sysutil.h"
#include "file.h"
#include "fileutil.h"
#include "Wincon.h"

#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"

static PyObject *s_pMainModule = NULL;
static PyObject *s_pProxyInitFunc = NULL;
static PyObject *s_pVersionFunc = NULL;
static PyObject *s_pSearchInitFunc = NULL;
static PyObject *s_pSearchNextFunc = NULL;
static PyObject *s_pGetUGCPatchInfoFunc = NULL;
static PyObject *s_pGetUGCProjectContainerFunc = NULL;
static PyObject *s_pGetUGCProjectSeriesContainerFunc = NULL;
static PyObject *s_pDeleteAllUGCFunc = NULL;
static PyObject *s_pImportUGCProjectContainerAndDataFunc = NULL;
static PyObject *s_pImportUGCProjectSeriesContainerFunc = NULL;
static PyObject *s_pXMLRPCBinaryFunc = NULL;

bool UGCExportImport_InitPython(void)
{
	if(!pyLibInitialize())
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Could not initialize Python environment!\n");
		return false;
	}

	if(!(s_pMainModule = pyLibLoadResource(UGC_EXPORT_IMPORT, "UGCExportImport", PYLIB_MAIN_MODULE)))
	{
		PyErr_Print();
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Could not run main script [UGCExportImport]!\n");
		return false;
	}

	pyLibInitVars();

	s_pProxyInitFunc = pyLibGetFuncSafe(s_pMainModule, "ProxyInit");
	if(!s_pProxyInitFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no ProxyInit function - it will be unable to function!\n");
		return false;
	}

	s_pVersionFunc = pyLibGetFuncSafe(s_pMainModule, "Version");
	if(!s_pVersionFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no Version function - it will be unable to function!\n");
		return false;
	}

	s_pSearchInitFunc = pyLibGetFuncSafe(s_pMainModule, "SearchInit");
	if(!s_pSearchInitFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no SearchInit function - it will be unable to function!\n");
		return false;
	}

	s_pSearchNextFunc = pyLibGetFuncSafe(s_pMainModule, "SearchNext");
	if(!s_pSearchNextFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no SearchNext function - it will be unable to function!\n");
		return false;
	}

	s_pGetUGCPatchInfoFunc = pyLibGetFuncSafe(s_pMainModule, "GetUGCPatchInfo");
	if(!s_pGetUGCPatchInfoFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no GetUGCPatchInfo function - it will be unable to function!\n");
		return false;
	}

	s_pGetUGCProjectContainerFunc = pyLibGetFuncSafe(s_pMainModule, "GetUGCProjectContainer");
	if(!s_pGetUGCProjectContainerFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no GetUGCProjectContainer function - it will be unable to function!\n");
		return false;
	}

	s_pGetUGCProjectSeriesContainerFunc = pyLibGetFuncSafe(s_pMainModule, "GetUGCProjectSeriesContainer");
	if(!s_pGetUGCProjectSeriesContainerFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no GetUGCProjectSeriesContainer function - it will be unable to function!\n");
		return false;
	}

	s_pDeleteAllUGCFunc = pyLibGetFuncSafe(s_pMainModule, "DeleteAllUGC");
	if(!s_pDeleteAllUGCFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no DeleteAllUGC function - it will be unable to function!\n");
		return false;
	}

	s_pImportUGCProjectContainerAndDataFunc = pyLibGetFuncSafe(s_pMainModule, "ImportUGCProjectContainerAndData");
	if(!s_pImportUGCProjectContainerAndDataFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no ImportUGCProjectContainerAndData function - it will be unable to function!\n");
		return false;
	}

	s_pImportUGCProjectSeriesContainerFunc = pyLibGetFuncSafe(s_pMainModule, "ImportUGCProjectSeriesContainer");
	if(!s_pImportUGCProjectSeriesContainerFunc)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" has no ImportUGCProjectSeriesContainer function - it will be unable to function!\n");
		return false;
	}

	{
		PyObject* xmlrpc_module = PyImport_ImportModule("xmlrpclib");
		if(!xmlrpc_module)
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" could not resolve xmlrpclib module - it will be unable to function!\n");
			return false;
		}
		s_pXMLRPCBinaryFunc = PyObject_GetAttrString(xmlrpc_module, "Binary");
		if(!s_pXMLRPCBinaryFunc)
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" could not resolve xmlrpclib.Binary function - it will be unable to function!\n");
			return false;
		}
	}

	return true;
}

static char *CallPyFunction(PyObject *pyFuncObject, PyObject *pParams)
{
	char *result = NULL;
	PyObject *pResult = PyObject_CallObject(pyFuncObject, pParams);
	if(pResult)
	{
		PyArg_Parse(pResult, "es", "utf-8", &result);

		Py_DECREF(pResult);
	}

	Py_XDECREF(pParams);

	if(PyErr_Occurred())
		PyErr_Print();

	return result;
}

// returns 1 if user inputs the success string (case-insensitive) after being prompted with prompt
int UGCExportImport_InputTest(const char *prompt, const char *success)
{
	char *result = NULL;
	char buffer[256] = "";
	int ret = 0;
	HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);

	printf("\n%s: ", prompt);

	SetConsoleMode(hConsole, ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

	result = fgets(buffer, ARRAY_SIZE_CHECKED(buffer), fileWrap(stdin));
	if(result)
	{
		result = strrchr(buffer, '\n');
		if(result) *result = '\0';
		if(0 == stricmp(buffer, success))
			ret = 1;
	}

	printf("\n");

	return ret;
}

static char *InputPassword(const char *prompt)
{
	char *result = NULL;
	char buffer[256] = "";
	char *ret = NULL;
	HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);

	printf("\n%s: ", prompt);

	SetConsoleMode(hConsole, ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT);

	result = fgets(buffer, ARRAY_SIZE_CHECKED(buffer), fileWrap(stdin));
	if(result)
	{
		result = strrchr(buffer, '\n');
		if(result) *result = '\0';
		ret = strdup(buffer);
	}

	printf("\n\n");

	return ret;
}

bool UGCExportImport_ProxyInit(const char *machine, const char *username)
{
	char *result = NULL;
	PyObject *pParams = NULL;
	char *password = NULL;

	if(strlen(username) && 0 != stricmp(machine, "localhost"))
	{
		char prompt[1024] = "";

		sprintf(prompt, "Enter password for %s at %s: ", username, machine);

		password = InputPassword(prompt);
	}

	pParams = Py_BuildValue("(sss)", machine, username, password);
	if(password)
		free(password);

	if(!pParams)
		return false;

	result = CallPyFunction(s_pProxyInitFunc, pParams);
	if(!result)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" ProxyInit function - did not return a string for the URL!\n");
		return false;
	}
	printf("Using ");
	printfColor(COLOR_GREEN | COLOR_BRIGHT, "%s", result);
	printf(" for XMLRPC\n\n");
	PyMem_Free(result);

	return true;
}

bool UGCExportImport_Version()
{
	char *result = CallPyFunction(s_pVersionFunc, NULL);
	if(!result)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" Version function - did not return a string for the version!\n");
		return false;
	}
	printf("WebRequestServer[1] returned ");
	printfColor(COLOR_GREEN | COLOR_BRIGHT, "%s", result);
	printf(" for the version\n\n");
	PyMem_Free(result);

	return true;
}

char *UGCExportImport_SearchInit(bool includeSaved, bool includePublished, UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	char *estrUGCProjectSearchInfo = NULL;
	PyObject *pParams = NULL;

	ParserWriteText(&estrUGCProjectSearchInfo, parse_UGCProjectSearchInfo, pUGCProjectSearchInfo, 0, 0, 0);

	pParams = Py_BuildValue("(iis)", includeSaved, includePublished, estrUGCProjectSearchInfo);
	estrDestroy(&estrUGCProjectSearchInfo);
	if(!pParams)
		return NULL;

	return CallPyFunction(s_pSearchInitFunc, pParams);
}

UGCSearchResult *UGCExportImport_SearchNext(const char *searchKey)
{
	char *result = NULL;

	PyObject *pParams = Py_BuildValue("(s)", searchKey);
	if(!pParams)
		return NULL;

	result = CallPyFunction(s_pSearchNextFunc, pParams);
	if(result && result[0])
	{
		UGCSearchResult *pUGCSearchResult = StructCreate(parse_UGCSearchResult);
		ParserReadText(result, parse_UGCSearchResult, pUGCSearchResult, 0);
		PyMem_Free(result);
		return pUGCSearchResult; // continue searching for IDs
	}
	if(result)
		PyMem_Free(result);
	return NULL; // done searching for IDs
}

UGCPatchInfo *UGCExportImport_GetUGCPatchInfo()
{
	char *result = NULL;

	result = CallPyFunction(s_pGetUGCPatchInfoFunc, NULL);
	if(result)
	{
		UGCPatchInfo *pUGCPatchInfo = StructCreate(parse_UGCPatchInfo);
		if(result[0])
			ParserReadText(result, parse_UGCPatchInfo, pUGCPatchInfo, 0);
		PyMem_Free(result);
		return pUGCPatchInfo;
	}
	return NULL;
}

UGCProject *UGCExportImport_GetUGCProjectContainer(ContainerID uUGCProjectID)
{
	char *result = NULL;

	PyObject *pParams = Py_BuildValue("(i)", uUGCProjectID);
	if(!pParams)
		return NULL;

	result = CallPyFunction(s_pGetUGCProjectContainerFunc, pParams);
	if(result)
	{
		UGCProject *pUGCProject = StructCreate(parse_UGCProject);
		if(result[0])
			ParserReadText(result, parse_UGCProject, pUGCProject, 0);
		PyMem_Free(result);
		return pUGCProject;
	}
	return NULL;
}

UGCProjectSeries *UGCExportImport_GetUGCProjectSeriesContainer(ContainerID uUGCProjectSeriesID)
{
	char *result = NULL;

	PyObject *pParams = Py_BuildValue("(i)", uUGCProjectSeriesID);
	if(!pParams)
		return NULL;

	result = CallPyFunction(s_pGetUGCProjectSeriesContainerFunc, pParams);
	if(result)
	{
		UGCProjectSeries *pUGCProjectSeries = StructCreate(parse_UGCProjectSeries);
		if(result[0])
			ParserReadText(result, parse_UGCProjectSeries, pUGCProjectSeries, 0);
		PyMem_Free(result);
		return pUGCProjectSeries;
	}
	return NULL;
}

bool UGCExportImport_DeleteAllUGC(const char *strComment)
{
	char *result = NULL;

	PyObject *pParams = Py_BuildValue("(s)", strComment);
	if(!pParams)
	{
		PyErr_Print();
		return false;
	}

	printfColor(COLOR_BLUE | COLOR_BRIGHT, "Deleting all UGC content on destination shard before import...");

	result = CallPyFunction(s_pDeleteAllUGCFunc, pParams);
	if(!result)
	{
		printfColor(COLOR_RED | COLOR_BRIGHT, "\nERROR: Python script \"UGCExportImport\" DeleteAllUGC function - did not return a string!\n");
		return false;
	}
	printfColor(COLOR_GREEN | COLOR_BRIGHT, "%s\n\n", result);
	PyMem_Free(result);

	return true;
}

char *UGCExportImport_ImportUGCProjectContainerAndData(UGCProject *pUGCProject, const char *estrUGCProjectDataPublished, const char *estrUGCProjectDataSaved,
		const char *strPreviousShard, const char *strComment, bool forceDelete)
{
	char *estrUGCProject = NULL;
	PyObject *pParams = NULL;

	ParserWriteText(&estrUGCProject, parse_UGCProject, pUGCProject, 0, 0, 0);

	pParams = Py_BuildValue("(sssssi)", estrUGCProject, estrUGCProjectDataPublished ? estrUGCProjectDataPublished : "", estrUGCProjectDataSaved ? estrUGCProjectDataSaved : "",
		strPreviousShard, strComment, forceDelete);
	estrDestroy(&estrUGCProject);
	if(!pParams)
	{
		PyErr_Print();
		return false;
	}

	return CallPyFunction(s_pImportUGCProjectContainerAndDataFunc, pParams);
}

char *UGCExportImport_ImportUGCProjectSeriesContainer(UGCProjectSeries *pUGCProjectSeries, const char *strPreviousShard, const char *strComment, bool forceDelete)
{
	char *estrUGCProjectSeries = NULL;
	PyObject *pParams = NULL;

	ParserWriteText(&estrUGCProjectSeries, parse_UGCProjectSeries, pUGCProjectSeries, 0, 0, 0);

	pParams = Py_BuildValue("(sssi)", estrUGCProjectSeries, strPreviousShard, strComment, forceDelete);
	estrDestroy(&estrUGCProjectSeries);
	if(!pParams)
	{
		PyErr_Print();
		return false;
	}

	return CallPyFunction(s_pImportUGCProjectSeriesContainerFunc, pParams);
}

char *UGCExportImport_DecompressMemoryToText(const char *data, U32 size, int* outputSize)
{
	char *pBufUncompressed;
	int iUncompressedLen;
	char strTempFile[MAX_PATH], strTempFileAbsolute[MAX_PATH];
	FILE *fOut;

	if(!data || !size)
		return false;

	// Hack to allow us to read .gz files from memory
	sprintf(strTempFile, "%s/gztemp-ugcimport-%d.gz", fileTempDir(), GetAppGlobalID());
	fileLocateWrite(strTempFile, strTempFileAbsolute);
	makeDirectoriesForFile(strTempFileAbsolute);
	fOut = fileOpen(strTempFileAbsolute, "wb");
	if(!fOut)
		return false;
	fwrite(data, size, 1, fOut);
	fclose(fOut);

	pBufUncompressed = fileAllocWBZ(strTempFile, &iUncompressedLen);
	if(outputSize)
		*outputSize = iUncompressedLen;
	fileForceRemove(strTempFileAbsolute);

	if(pBufUncompressed)
	{
		sprintf(strTempFile, "%s/gztemp-ugcimport-%d", fileTempDir(), GetAppGlobalID());
		fileLocateWrite(strTempFile, strTempFileAbsolute);
		makeDirectoriesForFile(strTempFileAbsolute);
		fOut = fileOpen(strTempFileAbsolute, "wb");
		if(fOut)
		{
			fwrite(pBufUncompressed, iUncompressedLen, 1, fOut);
			fclose(fOut);
		}
	}

	return pBufUncompressed;
}
