
#include <windows.h>
#include <psapi.h>
#include <objbase.h>
#include <comdef.h>
#include <comdefsp.h>
#include <basetsd.h>

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef _aligned_malloc_dbg


//#import "C:\Program Files (x86)\Common Files\Microsoft Shared\OFFICE11\MSO.DLL"
//#import <C:\Program Files (x86)\Common Files\Microsoft Shared\MSEnv\dte.olb> no_namespace
//#import "C:\Program Files\Common Files\Microsoft Shared\OFFICE11\MSO.DLL"
//#import <C:\Program Files\Common Files\Microsoft Shared\MSEnv\dte.olb> no_namespace
//#import <envdte.dll> no_namespace
//#import <envdte80.dll> no_namespace

#pragma warning( disable : 4278 )
#pragma warning( disable : 4146 )
//The following #import imports EnvDTE based on its LIBID.
#import "libid:80cc9f66-e7d8-4ddd-85b6-d9e6cd0e93e2" version("8.0") lcid("0") named_guids no_namespace rename("LONG_PTR","ALREADY_HAVE_LONG_PTR")  rename("ULONG_PTR","ALREADY_HAVE_ULONG_PTR")
//The following #import imports EnvDTE80 based on its LIBID.
#import "libid:1A31287A-4D7D-413e-8E32-3B374931BD89" version("8.0") lcid("0") named_guids no_namespace rename("LONG_PTR","ALREADY_HAVE_LONG_PTR")  rename("ULONG_PTR","ALREADY_HAVE_ULONG_PTR")
#pragma warning( default : 4146 )
#pragma warning( default : 4278 )

#include "stdio.h"
 
// VB-like GetObject call - uses Moniker interface to allow object to be looked up in ROT
IUnknown *VBGetObject(LPCOLESTR wszObjectName) {
	IUnknown *pUnk = 0;
	IBindCtx *pbc = 0;
	HRESULT hr = CreateBindCtx(0, &pbc);
	if (SUCCEEDED(hr)) {
		ULONG cch;
		IMoniker *pmk = 0;
		hr = MkParseDisplayName(pbc, wszObjectName,
			&cch, &pmk);
		if (SUCCEEDED(hr)) {
			hr = pmk->BindToObject(pbc, 0, IID_IUnknown,
				(void**)&pUnk);
			pmk->Release();
		}
		pbc->Release();
	}
	return pUnk;
}

bool GetProcessBaseExecutable(DWORD pid, char* buf, int buf_size)
{
	bool ret = false;
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
		PROCESS_VM_READ, FALSE, pid);

	// Get the process name.
	if (NULL != hProcess)
	{
		HMODULE hMod;
		DWORD cbNeeded;

		if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
		{
			GetModuleBaseName(hProcess, hMod, buf, buf_size);
			ret = true;
		}
		CloseHandle(hProcess);
	}
	return ret;
}

// all right, trying a new method to try and find a DTE:
// - get the COM interface to the global ROT and look through it manually
//   instead of trying to go through the MkParseDisplayName interface
_DTEPtr FindDTE(char* sub)
{
	ULONG fetched = 0;
	IUnknownPtr punk = 0;
	IMonikerPtr moniker = 0;
	IEnumMonikerPtr enumerator = 0;
	IRunningObjectTablePtr prot = 0; 
	char substring[MAX_PATH];

	try
	{

	strcpy_s(substring, sizeof(substring), sub);
	_strlwr_s(substring, sizeof(substring));

	// enumerate through all objects registered in the ROT
	GetRunningObjectTable(0, &prot);
	prot->EnumRunning(&enumerator);
	enumerator->Reset();
	while (SUCCEEDED(enumerator->Next(1, &moniker, &fetched)) && fetched)
	{
		// filter for those that start with !VisualStudio.DTE
		wchar_t prefix[] = L"!VisualStudio.DTE";
		IBindCtxPtr bind = 0;
		LPOLESTR name;
		bool match;
		CreateBindCtx(0, &bind);
		moniker->GetDisplayName(bind, 0, &name);
		
		match = wcsncmp(prefix, name, wcslen(prefix)) == 0;
		CoTaskMemFree(name);
		if (match)
		{
			// try to get the DTE interface
			punk = 0;
			prot->GetObject(moniker, &punk);
			if (punk)
			{
				_DTEPtr dte = punk;
				if (dte)
				{
					// do a substring match on the active solution
					char solutionname[MAX_PATH];
					strcpy_s(solutionname, sizeof(solutionname), (char*)dte->GetSolution()->FullName);
					_strlwr_s(solutionname, sizeof(solutionname));

					// match the substring
					if (strstr(solutionname, substring))
					{
						return dte;
					}
				}
			}
		}
	} // each item in rot

	}
	catch (_com_error e)
	{
		printf("COM ERROR: FindDTE %s\n", sub);
		return NULL;
	}
	return NULL;
}

// attach to the default DTE
_DTEPtr GetDefaultDTE(LPCOLESTR versionstr)
{
	CLSID clsid;
	_DTEPtr dte = NULL;
	HRESULT hr = CLSIDFromProgID(versionstr, &clsid); 
	if (SUCCEEDED(hr))
	{
		IUnknown* pIUnknown;
		hr = GetActiveObject(clsid, NULL, &pIUnknown); // get running instance
		if (SUCCEEDED(hr)) {
			dte = pIUnknown;
			pIUnknown->Release();
		}
	}
	return dte;
}


extern "C" {
bool AttachDebugger(long iProcessID)
{
	bool bSucceeded = false;
	//char buf[10000];
	CoInitialize(NULL);

	try
    {
        // the #import command creates all these pointers as smart pointers
        // so it takes care of correct COM reference counting for you..
        DebuggerPtr dbg = NULL;
		char exename[MAX_PATH];
		char* dot;
		_DTEPtr dte = NULL;
		long count;
		HRESULT hr;

		// get exe name of target executable
		if (!GetProcessBaseExecutable(iProcessID, exename, sizeof(exename)))
			return false;
		// cut off suffixes and extension
		dot = strstr(exename, "X64.exe");
		if (dot) *dot = 0;
		dot = strstr(exename, "X64FD.exe");
		if (dot) *dot = 0;
		dot = strstr(exename, "FD.exe");
		if (dot) *dot = 0;
		dot = strchr(exename, '.');
		if (dot) *dot = 0; // cut off extension

		// some heuristics to try to find an appropriate instance to attach to..
		dte = FindDTE(exename);
		if (!dte && (strstr(exename, "GameServer") || strstr(exename, "GameClient")))
			dte = FindDTE("ClientServer");
		if (!dte) // failing that, look through instances for a master solution
			dte = FindDTE("MasterSolution");
		if (!dte) // failing again, just get any running instance
			dte = GetDefaultDTE(L"VisualStudio.DTE.10.0");
		if (!dte) // failing again, just get any running instance
			dte = GetDefaultDTE(L"VisualStudio.DTE.8.0");
		if (!dte)
			return false;

		// now that we've chosen a DTE, attach to it
		dbg = dte->GetDebugger();
        count = dbg->LocalProcesses->Count;
        for (int i = 1; i <= count; i++)
        {
			_variant_t num = i;
			ProcessPtr proc = dbg->LocalProcesses->Item(num);
			long ID = proc->GetProcessID();
			if (ID == iProcessID) // put whatever you want here..
			{
				printf("AttachToDebugger found process\n");
				hr = proc->Attach();
				if (SUCCEEDED(hr))
				{
					printf("AttachToDebugger succeeded\n");
					// MessageBox(wnd, "successfully attached", "status", 0);
					bSucceeded = true;
				}
			}
		}
	}

	catch (_com_error e)
	{
		printf("COM ERROR: AttachDebugger\n");
	}
 
    CoUninitialize();
	return bSucceeded;
}
} // extern C