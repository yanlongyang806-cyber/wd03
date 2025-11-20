// turning off this warning, which is bogus for how we create our AutoStruct containers.
#pragma warning(disable:4510) // 'x' : default constructor could not be generated
#pragma warning(disable:4610) // struct 'x' can never be instantiated - user defined constructor required

extern "C" {



#include "ETCommon/symstore.h"
#include "ETCommon/ETCommonStructs.h"
#include "AutoGen/ETCommonStructs_h_ast.h"
#include "earray.h"
#include "estring.h"
#include "callstack.h"
#include "utils.h"
#include "file.h"
#include "fileutil2.h"
#include "error.h"
#include "logging.h"
#include "stringUtil.h"
#include "fileutil.h"



}


#include <windows.h>
#include <atlbase.h>
#include <dbghelp.h>

#include "dia2.h"
#include "sysutil.h"



/*#define BASE_SYMSERV_PATHS  "srv*C:\\symcache*\\\\symserv\\data\\symserv\\LongTerm;\
srv*C:\\symcache*\\\\symserv\\data\\symserv\\2week;\
srv*C:\\symcache*\\\\symserv\\data\\symserv\\2day;\
srv*C:\\symcache*\\\\symserv\\data\\symserv\\dataCryptic;\
srv*C:\\symcache*\\\\symserv\\data\\symserv\\Xenon"*/
//#define REMOTE_SYMSERV_PATH "*http://msdl.microsoft.com/download/symbols"
#define MAX_SYMBOL_PATH (1024)

//char gErrorTrackerPDBTempDir[MAX_PATH] = "C:\\Core\\ErrorTracker\\PDBtemp\\";

extern "C" {
extern bool gbETVerbose;
extern int gUseRemoteSymServ;
extern char gTempPDBDirectory[MAX_PATH];
};

#define LOG_SYMBOL_LOOKUP_RESULTS false

#define GUID_IS_NULL(sig70) \
	(!(sig70.Data1 || sig70.Data2 || sig70.Data3 || \
	sig70.Data4[0] || sig70.Data4[1] || sig70.Data4[2] || sig70.Data4[3] || \
	sig70.Data4[4] || sig70.Data4[5] || sig70.Data4[6] || sig70.Data4[7]))

static bool bSymsrvDebugFile = true;

static char symerrorLogFile[MAX_PATH] = "logs\\symsrv_nullguid.log";
static FileWrapper *sfLogErrorfile = NULL;

static char symsrvLogFile[MAX_PATH] = "logs\\symsrv.log";
static FileWrapper *sfLogfile = NULL;

void setSymbolLogFile(const char *file)
{
	strcpy(symsrvLogFile, file);
}
const char * getSymbolLogFile(void)
{
	return symsrvLogFile;
}
FileWrapper * openLogFile(void)
{
	if (LOG_SYMBOL_LOOKUP_RESULTS)
	{
		sfLogfile = (FileWrapper*) fopen(symsrvLogFile, "a");
		return sfLogfile;
	}
	return NULL;
}
void closeLogFile(void)
{
	if (sfLogfile && LOG_SYMBOL_LOOKUP_RESULTS)
	{
		fclose(sfLogfile);
		sfLogfile = NULL;
	}
}
FileWrapper * getLogFile(void)
{
	if (!sfLogfile)
		return openLogFile();
	return sfLogfile;
}
void writeToLogFile(const char *fmt, ... )
{
	if (LOG_SYMBOL_LOOKUP_RESULTS)
	{
		char buffer[1024];
		va_list		ap;

		va_start(ap, fmt);
		vsprintf(buffer,fmt,ap);
		va_end(ap);

		fwrite(buffer, 1, strlen(buffer), getLogFile());
	}
}

const char *getTempPDBDirectory(void)
{
	if (gTempPDBDirectory[0] == 0)
	{
		char cwd[1024];
		if (fileGetcwd(cwd, sizeof(cwd)) != NULL)
			printf("Current working dir: %s\n", cwd);
		sprintf(gTempPDBDirectory, "%s\\tmp\\PDB\\", cwd);
		printf("%s\n", gTempPDBDirectory);
	}
	return gTempPDBDirectory;
}

// ------------------------------------------------------------------------------------------

typedef struct PDB_SIGNATURE
{
	GUID Guid;
	DWORD Age;
	CHAR Path[MAX_PATH];
} PDB_SIGNATURE;

struct LoadedModule
{
	U64 uBaseAddress;
	U32 uSize;
	char *pModuleName; // THIS IS TO BE OPTIONAL
	//DM_PDB_SIGNATURE PdbSig70; This was previously imported from an XBox library
	PDB_SIGNATURE PdbSig70;
	CComPtr<IDiaSession> pSession;
	DWORD tickLastUsed;
};

static LoadedModule **spLoadedModuleList = NULL;
static int *gspLoadedModuleIndices = NULL;

int sortLoadedModules(const void *d1, const void *d2)
{
	LoadedModule *m1 = (LoadedModule*) d1;
	LoadedModule *m2 = (LoadedModule*) d2;
	if (m1->tickLastUsed < m2->tickLastUsed)
		return -1;
	if (m1->tickLastUsed > m2->tickLastUsed)
		return 1;
	return 0;
}

// ------------------------------------------------------------------------------------------

// From the example:
// "Most DbgHelp functions use a 'process' handle to identify their context.
// This can be virtually any number, except zero."
static HANDLE sDebugProcess = (HANDLE)1;

// ------------------------------------------------------------------------------------------
// Forwards

bool LoadSymbolsForModule(const char *pModuleName, const void* baseAddress, size_t size, 
						  DWORD /*timeStamp*/, const PDB_SIGNATURE* signature);
void PopulateStackTraceLine(NOCONST(StackTraceLine) *pStackTraceLine, U64 address);

// ------------------------------------------------------------------------------------------

static void destroyStackLine(StackTraceLine *pLine)
{
	StructDestroy(parse_StackTraceLine, pLine);
}
static void eaFreeDestructor(void *pData)
{
	free(pData);
}

static void estrCopyModuleName(char **estr, const char *pModuleName)
{
	const char *pTmp;
	
	if (!pModuleName)
		return;
	pTmp = strrchr(pModuleName, '\\');
	if(pTmp)
	{
		pTmp++; // Advance past the backslash
	}
	else
	{
		// Just take the whole thing
		pTmp = pModuleName;
	}
	
	estrCopy2(estr, pTmp);
}

bool symstoreLookupStackTrace(CallStack *pSrc, NOCONST(StackTraceLine) ***pDst, SymStoreStatusCallback cbStatusUpdate, void *data, const char *extraErrorLogData, SymStoreModuleFailureCallback cbFailureReport)
{
	int iCurrentModule, i;
	char *lastGUID = NULL;
	SymServStatusStruct *status = (SymServStatusStruct *)data;
	// ------------------------------------------------------------------------------------------
	// Lazy init
	if(!InitCOMAndDbgHelp()) return false;
	if(!InitSymbolServer() ) return false;

	// ------------------------------------------------------------------------------------------
	// Load all module symbols

	int iBackupCount;
	if (!dirExists(getTempPDBDirectory()))
	{
		mkdirtree_const(getTempPDBDirectory());
	} else
	{
		char** psBackups = fileScanDir(getTempPDBDirectory());
        iBackupCount = eaSize( (cEArrayHandle*)&psBackups );
		for (i = 0; i < iBackupCount; i++)
		{
			//fileForceRemove(psBackups[i]);
			int val = remove(psBackups[i]);
		}
		eaDestroyEx((EArrayHandle*) &psBackups, eaFreeDestructor);
	}

	eaiClear(&gspLoadedModuleIndices);
	openLogFile();
	for(iCurrentModule=0; iCurrentModule<eaSize((void***)&pSrc->ppModules); iCurrentModule++)
	{
		PDB_SIGNATURE signature;
		memset(&signature, 0, sizeof(PDB_SIGNATURE));
		bool bLoaded = false;

		if(pSrc->ppModules[iCurrentModule]->pGuid)
		{
			int guid_element_count = 
				sscanf_s(pSrc->ppModules[iCurrentModule]->pGuid, "{%x-%x-%x-%x %x-%x %x %x %x %x %x}",
				&signature.Guid.Data1, &signature.Guid.Data2, &signature.Guid.Data3,
				&signature.Guid.Data4[0], &signature.Guid.Data4[1],
				&signature.Guid.Data4[2], &signature.Guid.Data4[3],
				&signature.Guid.Data4[4], &signature.Guid.Data4[5],
				&signature.Guid.Data4[6], &signature.Guid.Data4[7]);

			if (GUID_IS_NULL(signature.Guid))
			{
				if (!sfLogErrorfile)
				{
					sfLogErrorfile = (FileWrapper*) fopen(symerrorLogFile, "a");
					assert(sfLogErrorfile);
					fprintf(sfLogErrorfile, "\n** NULL GUIDs for %s **\n", extraErrorLogData);
				}
				fprintf(sfLogErrorfile, "Module: %s, PDB: %s\n",
					pSrc->ppModules[iCurrentModule]->pModuleName, pSrc->ppModules[iCurrentModule]->pPDBName);
			}

			if(guid_element_count == 11)
			{
				signature.Age = pSrc->ppModules[iCurrentModule]->uAge;
				strcpy_s(SAFESTR(signature.Path), 
					pSrc->ppModules[iCurrentModule]->pPDBName ? pSrc->ppModules[iCurrentModule]->pPDBName
					: "");

				writeToLogFile("Loading PDB: GUID = %s, Path = %s\n", pSrc->ppModules[iCurrentModule]->pGuid, signature.Path);
				if (!lastGUID || stricmp(lastGUID, pSrc->ppModules[iCurrentModule]->pGuid))
				{
					char *module = NULL;
					if (pSrc->ppModules[iCurrentModule]->pModuleName)
					{
						module = strrchr(pSrc->ppModules[iCurrentModule]->pModuleName, '\\');
					}
					servLog(LOG_SYMSERVLOOKUP, "LoadingPDB", "GUID \"%s\" Module %s Path %s LookupHash %s", pSrc->ppModules[iCurrentModule]->pGuid,
						module ? module + 1 : NULL_TO_EMPTY(pSrc->ppModules[iCurrentModule]->pModuleName), signature.Path, status->hashString);
					lastGUID = pSrc->ppModules[iCurrentModule]->pGuid;
				}
				bLoaded = LoadSymbolsForModule(pSrc->ppModules[iCurrentModule]->pModuleName, 
					(void*)pSrc->ppModules[iCurrentModule]->uBaseAddress,
					pSrc->ppModules[iCurrentModule]->uSize, 0, &signature);
			}
		}

		if(bLoaded)
		{
			if (gbETVerbose)
				printf("Loaded symbols for '%s'...\n", signature.Path);
		}
		else
		{
			char *module = NULL;
			if (pSrc->ppModules[iCurrentModule]->pModuleName)
			{
				module = strrchr(pSrc->ppModules[iCurrentModule]->pModuleName, '\\');
			}
			module = module ? module + 1 : NULL_TO_EMPTY(pSrc->ppModules[iCurrentModule]->pModuleName); //If we found a backslash, advance past it, otherwise just use full path.
			if (gbETVerbose)
				printf("Did not load symbols for '%s'...\n", signature.Path);
			writeToLogFile ("; Failed\n");
			servLog(LOG_SYMSERVLOOKUP, "LoadingFailed", "GUID \"%s\" Module %s Path %s LookupHash %s", pSrc->ppModules[iCurrentModule]->pGuid, 
				module, signature.Path, status->hashString);
			if (isCrypticModule(module))
			{
				cbFailureReport(data, module, pSrc->ppModules[iCurrentModule]->pGuid);
			}
		}
		if (cbStatusUpdate)
			cbStatusUpdate(iCurrentModule, data);
	}

	// ------------------------------------------------------------------------------------------
	// Lookup all addresses
	{
		int iEntryCount = eaSize(&pSrc->ppEntries);
		int iModuleCount = eaSize(&pSrc->ppModules);

		if (iEntryCount != iModuleCount)
			writeToLogFile("\nDifferent number of entries and modules: %d != %d\n", iEntryCount, iModuleCount);
		for(i=0; i<iEntryCount; i++)
		{
			CallStackEntry *pEntry = pSrc->ppEntries[i];
			NOCONST(StackTraceLine) *pLine = StructCreateNoConst(parse_StackTraceLine);
			PopulateStackTraceLine(pLine, pEntry->uAddress);
			if (!pLine->pModuleName && pSrc->ppModules && i < iModuleCount && pSrc->ppModules[i]->pModuleName)
			{
				estrCopyModuleName(&pLine->pModuleName, pSrc->ppModules[i]->pModuleName);
			}
			eaPush((cEArrayHandle*)pDst, pLine);

			if((strStartsWith(pLine->pFunctionName, "superassert")) || strStartsWith(pLine->pFunctionName, "fatalerrorf")
				|| (strstri(pLine->pFunctionName, "printf")))
			{
				// Throw out our current stack data.
				// eaDestroyStruct doesn't work here because of C++ type-safety issues with our macros
				eaDestroyEx((EArrayHandle*)pDst, (EArrayItemCallback)destroyStackLine);
			}
			else if (strStartsWith(pLine->pFunctionName, "0x") && pSrc->ppModules && pSrc->ppModules[i]->pModuleName)
			{
				char *module = strrchr(pSrc->ppModules[i]->pModuleName, '\\');
				if (module)
				{
					module = module + 1;
					estrPrintf(&pLine->pFunctionName, "%s!%08"FORM_LL"x", module, pEntry->uAddress - pSrc->ppModules[i]->uBaseAddress);
				}
				else
				{
					estrPrintf(&pLine->pFunctionName, "%s!%08"FORM_LL"x", pSrc->ppModules[i]->pModuleName, pEntry->uAddress - pSrc->ppModules[i]->uBaseAddress);
				}
			}
		}
	}
	
	closeLogFile();
	if (sfLogErrorfile)
	{
		fclose(sfLogErrorfile);
		sfLogErrorfile = NULL;
	}
	return true;
}

// Trim our LoadedModule and DIA2 pointers cache if memory usage is too high
void symStore_TrimCache(void)
{
	// Free 25% of the LRU modules
	int iSize, iNumToFree, i;
	iSize = eaSize((cEArrayHandle*)&spLoadedModuleList);
	iNumToFree = iSize / 4;

	eaQSort(spLoadedModuleList, sortLoadedModules);
	for (i=iSize-1; i >= 0 && iNumToFree>0; i--)
	{
		if(spLoadedModuleList[i]->pModuleName)
			free(spLoadedModuleList[i]->pModuleName);
		delete spLoadedModuleList[i];
		eaRemoveFast((cEArrayHandle*) &spLoadedModuleList, i);
		iNumToFree--;
	}
}

// ----------------------------------------------------------------------------------------
// Taken from the CallStackDisplay2005 sample (removed C++ stuff and merged with COM init)

// This line makes us automatically load whatever dbghelp.dll is in the path, which may or may not
// have a legitimate set of functions. Do not use!
//#pragma comment(lib, "dbghelp.lib")

typedef BOOL (__stdcall *SymInitializeFunc)(IN HANDLE hProcess, IN PSTR UserSearchPath, IN BOOL fInvadeProcess);
static SymInitializeFunc pSymInitialize = NULL;
typedef DWORD (__stdcall *SymSetOptionsFunc)(IN DWORD SymOptions);
static SymSetOptionsFunc pSymSetOptions = NULL;
typedef BOOL(__stdcall *SymFindFileInPathFunc)(HANDLE hprocess, LPSTR SearchPath, LPSTR FileName, PVOID id, DWORD two, DWORD three, DWORD flags, LPSTR FoundFile, PFINDFILEINPATHCALLBACK callback, PVOID context);
static SymFindFileInPathFunc pSymFindFileInPath = NULL;


// Desc: Forcibly loads DbgHelp.dll from %XEDK%\bin\win32. This is done by calling
//       LoadLibrary with a fully specified path, since otherwise the version in the
//       system directory will take precedence.
// ----------------------------------------------------------------------------------------

static bool sbDebugHelpLoaded = false;

bool InitCOMAndDbgHelp()
{
	char dbgHelpPath[MAX_PATH];
	HMODULE hDbgHelp;

	if(sbDebugHelpLoaded)
	{
		return true;
	}

	if(FAILED(CoInitialize(NULL)))
    {
        printf("CoInitialize() failed.\n");
		return false;
    }

	// Create a fully specified path to the XEDK version of dbghelp.dll
	// This is necessary because symsrv.dll must be in the same directory
	// as dbghelp.dll, and only a fully specified path can guarantee which
	// version of dbghelp.dll we load.
	strcpy(dbgHelpPath, "dbghelp.dll");

	printf("Loading dbghelp: %s\n", dbgHelpPath);

    // Call LoadLibrary on DbgHelp.DLL with our fully specified path.

	
	//including utf8.h in .cpp files makes the world explode, so just doing this the old fashioned way
	{
		WCHAR temp[CRYPTIC_MAX_PATH];
		UTF8ToWideStrConvert((unsigned char*)dbgHelpPath, (unsigned short*)temp, ARRAY_SIZE(temp));
		hDbgHelp = LoadLibrary(temp);
	}

    // Print an error message and return FALSE if DbgHelp.DLL didn't load.
    if( !hDbgHelp )
    {
        printf("ERROR: Couldn't load DbgHelp.dll\n");
        return false;
    }

	pSymInitialize     = (SymInitializeFunc)    GetProcAddress(hDbgHelp, "SymInitialize");
	pSymSetOptions     = (SymSetOptionsFunc)    GetProcAddress(hDbgHelp, "SymSetOptions");
	pSymFindFileInPath = (SymFindFileInPathFunc)GetProcAddress(hDbgHelp, "SymFindFileInPath");
	if(!pSymInitialize || !pSymSetOptions || !pSymFindFileInPath)
	{
        printf("ERROR: Couldn't find Symbol function addresses.\n");
        return false;
	}

    // DbgHelp.DLL loaded.
	sbDebugHelpLoaded = true;
    return true;
}

static bool sbSymStoreLoaded = false;

bool InitSymbolServer(void)
{
	if(sbSymStoreLoaded)
	{
		return true;
	}

    // Enable DbgHelp debug messages, make sure that DbgHelp only loads symbols that
    // exactly match, and do deferred symbol loading for greater efficiency.
	pSymSetOptions( SYMOPT_DEBUG | SYMOPT_EXACT_SYMBOLS | SYMOPT_DEFERRED_LOADS );

	// Build up a complete symbol search path to give to DbgHelp.
	char *fullSearchPath = NULL;
	
	/*char *baseSymServLine = BASE_SYMSERV_PATHS;
	if(gUseRemoteSymServ)
		baseSymServLine = BASE_SYMSERV_PATHS REMOTE_SYMSERV_PATH;*/

    char* ntSymbolPath = (char*) malloc(MAX_SYMBOL_PATH);
    size_t symbolPathSize;
	//_putenv_s("_NT_SYMBOL_PATH", baseSymServLine);
    errno_t err = getenv_s( &symbolPathSize, ntSymbolPath, MAX_SYMBOL_PATH, "_NT_SYMBOL_PATH" );
    if( !err )
	{
		estrCopy2(&fullSearchPath, ntSymbolPath);
	}
	free( ntSymbolPath );

    // Add the current directory to the search path.

	estrConcatf(&fullSearchPath, ";.");

	printf("SymInitialize(\"%s\")\n", fullSearchPath);

	// Pass the symbol search path on to DbgHelp.
	if(!pSymInitialize( sDebugProcess, fullSearchPath, FALSE ))
	{
		char * lpMsgBuf = NULL;
		DWORD dw = GetLastError(); 

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR) &lpMsgBuf,
			0, NULL );

		printf("SymInitialize() Failed: %s\n", lpMsgBuf);
		LocalFree(lpMsgBuf);
	}

	estrDestroy(&fullSearchPath);

	sbSymStoreLoaded = true;
	return true;
}

void PopulateStackTraceLine(NOCONST(StackTraceLine) *pStackTraceLine, U64 address)
{
    bool success = false;
	char *pModuleName = NULL;
    CHAR symbolName[1000] = {0};
    ULONG displacement = 0;
    CHAR filename[500] = {0};
    ULONG lineNumber = 0;
	DWORD curTick = GetTickCount();

    // Scan through the list of loaded modules to find the one that contains the
    // requested address.
	//for(int iCurrentLoadedModule=0; iCurrentLoadedModule<eaSize((void***)&spLoadedModuleList); iCurrentLoadedModule++)
	for (int iCurrentLoadedModuleIndex=0; iCurrentLoadedModuleIndex < eaiSize(&gspLoadedModuleIndices); iCurrentLoadedModuleIndex++)
    {
		LoadedModule *pModule = spLoadedModuleList[gspLoadedModuleIndices[iCurrentLoadedModuleIndex]];
		pModule->tickLastUsed = curTick;

        // Find what module's address range the address falls in.
        if(address > pModule->uBaseAddress &&
                    address < pModule->uBaseAddress + pModule->uSize)
        {
            CComPtr<IDiaSession>& pSession = pModule->pSession;

            // Find the symbol using the virtual address--the raw address. This
            // only works if you have previously told DIA where the module was
            // loaded, using put_loadAddress.
            // Specify SymTagPublicSymbol instead of SymTagFunction if you want
            // the full decorated names.
            CComPtr<IDiaSymbol> pFunc;
            HRESULT result = pSession->findSymbolByVA( address, SymTagFunction, &pFunc );
			if ( result != S_OK || !pFunc )
				result = pSession->findSymbolByVA( address, SymTagPublicSymbol, &pFunc );
            if ( result == S_OK && pFunc )
            {
                // Get the name of the function.
                CComBSTR functionName = 0;
                pFunc->get_name( &functionName );
                if( functionName )
                {
                    // Convert the function name from wide characters to char.
			        sprintf_s(SAFESTR(symbolName), "%S", (BSTR)functionName );

                    // Get the offset of the address from the symbol's address.
                    ULONGLONG symbolBaseAddress;
                    pFunc->get_virtualAddress( &symbolBaseAddress );
                    displacement = address - (ULONG)symbolBaseAddress;
                    success = true;

					pModuleName = pModule->pModuleName;

                    // Now try to get the filename and line number.
                    // Get an enumerator that corresponds to this instruction.
                    CComPtr< IDiaEnumLineNumbers > pLines;
                    const DWORD instructionSize = 4;
                    if ( SUCCEEDED( pSession->findLinesByVA( address, instructionSize, &pLines ) ) )
                    {
                        // We could loop over all of the source lines that map to this instruction,
                        // but there is probably at most one, and if there are multiple source
                        // lines we still only want one.
                        CComPtr< IDiaLineNumber > pLine;
                        DWORD celt;
						HRESULT hRes = pLines->Next( 1, &pLine, &celt );
                        if ( hRes == S_OK && celt == 1 )
                        {
                            // Get the line number.
                            hRes = pLine->get_lineNumber( &lineNumber );

							if (hRes == S_OK)
							{
								// Get the source file object, and then its name.
								CComPtr< IDiaSourceFile > pSrc;
								hRes = pLine->get_sourceFile( &pSrc );
								if (hRes == S_OK)
								{
									CComBSTR sourceName = 0;
									pSrc->get_fileName( &sourceName );
									// Convert from wide characters to ASCII.
									sprintf_s(SAFESTR(filename), "%S", (BSTR)sourceName );
									break;
								}
							}
                        }
                    }
                }
            }
        }
    }

    if( success )
    {
		writeToLogFile ("Module Name: %s; Function Name: %s (%d); File Name: %s\n", pModuleName, symbolName, 
			lineNumber, filename);
		estrPrintf(&pStackTraceLine->pFunctionName, "%s", symbolName);

		if(pModuleName)
			estrCopyModuleName(&pStackTraceLine->pModuleName, pModuleName);

        if( filename[0] )
        {
			// DO NOT truncate the file path; needed for SVN blaming
			estrPrintf(&pStackTraceLine->pFilename, "%s", filename);
			pStackTraceLine->iLineNum = lineNumber;
        }
    }

	// Final cleanup checking - ensure that the stack trace line is populated with -something-
	if(estrLength(&pStackTraceLine->pFunctionName) < 1)
	{
		writeToLogFile("Lookup failed: 0x%"FORM_LL"x\n", address);
		estrPrintf(&pStackTraceLine->pFunctionName, "0x%08"FORM_LL"x", address);
		estrPrintf(&pStackTraceLine->pFilename, "???");
		pStackTraceLine->iLineNum = 0;
	}
}

unsigned int comparePDBs(const PDB_SIGNATURE *lhs, const PDB_SIGNATURE *rhs)
{
	int i;
	if (lhs->Age != rhs->Age)
		return 1;
	if (lhs->Guid.Data1 != rhs->Guid.Data1) 
		return 1;
	if (lhs->Guid.Data2 != rhs->Guid.Data2) 
		return 1;
	if (lhs->Guid.Data3 != rhs->Guid.Data3) 
		return 1;
	for (i=0; i<8; i++)
	{
		if (lhs->Guid.Data4[i] != rhs->Guid.Data4[i]) 
			return 1;
	}
	return 0;
}

bool CreateTempPDBFile(char** pTempPath, const char* pdbPath) {
	int i=0;
	do {
		// Create a temporary file in the PDB temp dir
		i++;
		estrClear(pTempPath);
		estrPrintf(pTempPath, "%spdb.bkup%d", getTempPDBDirectory(), i);
	} while (fileExists(*pTempPath));

	return (!fileCopy(pdbPath, *pTempPath));
}

bool LoadSymbolsForModule(const char *pModuleName, 
						  const void* baseAddress, size_t size, 
						  DWORD /*timeStamp*/, const PDB_SIGNATURE* signature)
{
	int i;
	/*if (!pModuleName)
	{
		writeToLogFile("No module name");
		return false;
	}*/
	for (i=0; i<eaSize((cEArrayHandle *) &spLoadedModuleList); i++)
	{
		if (spLoadedModuleList[i]->uBaseAddress == (ULONG_PTR) baseAddress &&
			comparePDBs(&spLoadedModuleList[i]->PdbSig70, signature) == 0)
		{
			if (gbETVerbose)
				printf ("PDB '%s' already loaded ...\n", spLoadedModuleList[i]->PdbSig70.Path);
			writeToLogFile("PDB: %s; Previously Loaded\n", spLoadedModuleList[i]->PdbSig70.Path);
			
			eaiPush(&gspLoadedModuleIndices, i);
			return true;
		}
	}

    // Create a DIA2 data source
    CComPtr<IDiaDataSource> pSource;
    HRESULT hr = CoCreateInstance( __uuidof(DiaSource),
                           NULL,
                           CLSCTX_INPROC_SERVER,
                           __uuidof( IDiaDataSource ),
                          (void **) &pSource);

    if (FAILED(hr))
	{
		writeToLogFile("Failed to create DIA2 data source");
        return false;
	}

    const char* pdbPath = signature->Path;

	//char asdf[2480];
	//SymGetSearchPath( sDebugProcess, asdf, 2480);
	
	char resultPath[MAX_PATH];
	BOOL findResult = pSymFindFileInPath( sDebugProcess, 0, 
                const_cast<char*>(pdbPath),
                const_cast<GUID*>(&signature->Guid), signature->Age, 0,
		        SSRVOPT_GUIDPTR,
                resultPath, 
                NULL,
                NULL);

    // If DbgHelp found the symbols then adjust our path.
    if( findResult )
        pdbPath = resultPath;

	writeToLogFile("Module: %s; PDB Path: %s", pModuleName, pdbPath);

	// Copy pdb file to temporary file
	char* tempPath = NULL;
	if (!CreateTempPDBFile(&tempPath, pdbPath))
	{
		estrDestroy(&tempPath);
		return false;
	}

    // Convert the filename to wide characters for use with DIA2.
    wchar_t wPdbPath[ _MAX_PATH ];
    size_t convertedChars;
    mbstowcs_s( &convertedChars, wPdbPath, tempPath, _TRUNCATE );

    // See if there is a PDB file at the specified location, and if so,
    // load it, checking the GUID and age to make sure that it is the correct
    // PDB file. Note that the timeStamp is not used anymore.
    hr = pSource->loadAndValidateDataFromPdb( wPdbPath, 
		const_cast<GUID*>(&signature->Guid), 0, signature->Age );
    if( FAILED( hr ) )
    {
        // Check the error code for details on why the load failed, which
        // could be because the file doesn't exist, signature doesn't match,
        // etc.
		int val = remove(tempPath);
		estrDestroy(&tempPath);
        return false;
    }

    // Create a session for the just loaded PDB file.
    CComPtr<IDiaSession> pSession;
    if( FAILED( pSource->openSession( &pSession ) ) ) 
    {
		fileForceRemove(tempPath);
		estrDestroy(&tempPath);
        return false;
    }

    // Tell DIA2 where the module was loaded.
    pSession->put_loadAddress( (ULONG_PTR)baseAddress );

    // Add this session to a list of loaded modules.
	LoadedModule *pLoadedModule = new LoadedModule;

	pLoadedModule->uSize = (U32)size;
	pLoadedModule->uBaseAddress = (ULONG_PTR)baseAddress;
	pLoadedModule->pModuleName = pModuleName ? strdup(pModuleName) : NULL;
	pLoadedModule->pSession = pSession;
	pLoadedModule->tickLastUsed = 0;
	
	memset(&pLoadedModule->PdbSig70, 0, sizeof(PDB_SIGNATURE));
	memcpy(&pLoadedModule->PdbSig70, signature, sizeof(GUID) + sizeof(DWORD));
	if (pdbPath)
		strcpy(pLoadedModule->PdbSig70.Path, pdbPath);

	eaiPush(&gspLoadedModuleIndices, eaSize((cEArrayHandle*) &spLoadedModuleList));
	eaPush((cEArrayHandle *)&spLoadedModuleList, pLoadedModule);

	int val = remove(tempPath);
	estrDestroy(&tempPath);
	writeToLogFile ("; Loaded\n");
    return true;
}
