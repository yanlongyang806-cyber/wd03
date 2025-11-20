///////////////////////////////////////////////////////////////////////////////
//
//  Module: CrashRpt.cpp
//
//    Desc: See CrashRpt.h
//
// Copyright (c) 2003 Michael Carruth
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "CrashRpt.h"
#include "CrashHandler.h"

#ifdef _DEBUG
#define CRASH_ASSERT(pObj)          \
   if (!pObj || sizeof(*pObj) != sizeof(CCrashHandler))  \
      DebugBreak()                                       
#else
#define CRASH_ASSERT(pObj)
#endif // _DEBUG

// extern "C" {
// extern int g_inside_pool_malloc;
// }

BOOL WINAPI
DllMain(HINSTANCE hinstDLL,ULONG fdwReason,LPVOID lpvReserved)
{
	//g_inside_pool_malloc = 1;
	return TRUE;
}

LPVOID CrashRptInstall(LPGETLOGFILE pfn)
{
   CCrashHandler *pImpl = new CCrashHandler(pfn);
   CRASH_ASSERT(pImpl);

   // Setup a default way to report errors.
   CrashRptSetFTPConduit(pImpl, _T("errors.coh.com"), _T("errors"), _T("kicks"));

   return pImpl;
}

void CrashRptUninstall(LPVOID lpState)
{
   CCrashHandler *pImpl = (CCrashHandler*)lpState;
   CRASH_ASSERT(pImpl);

   delete pImpl;
}

void CrashRptAddFile(LPVOID lpState, LPCTSTR lpFile, LPCTSTR lpDesc)
{
   CCrashHandler *pImpl = (CCrashHandler*)lpState;
   CRASH_ASSERT(pImpl);

   pImpl->AddFile(lpFile, lpDesc);
}

void CrashRptGenerateErrorReport(LPVOID lpState, PEXCEPTION_POINTERS pExInfo)
{
	CCrashHandler *pImpl = (CCrashHandler*)lpState;
	CRASH_ASSERT(pImpl);

	pImpl->GenerateErrorReport(pExInfo, "UnknownVersion", "UnknownMessage", GetCurrentThreadId());
}

void CrashRptGenerateErrorReport2(LPVOID lpState, PEXCEPTION_POINTERS pExInfo, const char *szVersion, const char *szMessage)
{
   CCrashHandler *pImpl = (CCrashHandler*)lpState;
   CRASH_ASSERT(pImpl);

   pImpl->GenerateErrorReport(pExInfo, szVersion, szMessage, GetCurrentThreadId());
}

void CrashRptGenerateErrorReport3(LPVOID lpState, PEXCEPTION_POINTERS pExInfo, const char *szVersion, const char *szMessage, DWORD dwThreadID)
{
	CCrashHandler *pImpl = (CCrashHandler*)lpState;
	CRASH_ASSERT(pImpl);

	pImpl->GenerateErrorReport(pExInfo, szVersion, szMessage, dwThreadID);
}

void CrashRptAbortErrorReport(LPVOID lpState)
{
	CCrashHandler *pImpl = (CCrashHandler*)lpState;
	CRASH_ASSERT(pImpl);

	pImpl->AbortErrorReport();	
}

void CrashRptSetFTPConduit(LPVOID lpState, LPCTSTR hostname, LPCTSTR username, LPCTSTR password)
{
	CCrashHandler *pImpl = (CCrashHandler*)lpState;
	CRASH_ASSERT(pImpl);

	if(pImpl->m_reportConduit)
	{
		delete pImpl->m_reportConduit;
		pImpl->m_reportConduit = NULL;
	}
	pImpl->m_reportConduit = new CFtpConduit(hostname, username, password);
}