#include "error.h"
#include "OsPermissions.h"
#include "wininclude.h"
#include "UTF8.h"
#include "estring.h"

// Enable or disable a Windows privilege for the current process' access token.
// Originally based on ModifyPrivilege() from "Setting the Backup and Restore Privileges"
//   http://msdn.microsoft.com/en-us/library/windows/desktop/aa387705(v=vs.85).aspx.
static bool ModifyPrivilege(const char *szPrivilege, bool fEnable)
{
	TOKEN_PRIVILEGES NewState;
	LUID             luid;
	HANDLE hToken    = NULL;

	// Open the process token for this process.
	if (!OpenProcessToken(GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
		&hToken))
	{
		WinErrorf(GetLastError(), "OpenProcessToken()");
		return false;
	}

	// Get the local unique ID for the privilege.
	if (!LookupPrivilegeValue_UTF8(NULL,
		szPrivilege,
		&luid))
	{
		CloseHandle(hToken);
		WinErrorf(GetLastError(), "LookupPrivilegeValue()");
		return false;
	}

	// Assign values to the TOKEN_PRIVILEGE structure.
	NewState.PrivilegeCount = 1;
	NewState.Privileges[0].Luid = luid;
	NewState.Privileges[0].Attributes = 
		(fEnable ? SE_PRIVILEGE_ENABLED : 0);

	// Adjust the token privilege.
	if (!AdjustTokenPrivileges(hToken,
		FALSE,
		&NewState,
		0,
		NULL,
		NULL))
	{
		WinErrorf(GetLastError(), "AdjustTokenPrivileges()");
		return false;
	}

	// Close the handle.
	CloseHandle(hToken);

	return true;
}

// Enable operating system privilege to bypass regular file ACLs.
bool EnableBypassReadAcls(bool enable)
{
	bool success;
	extern bool file_windows_backup_semantics; // Internal interface in file.h.

	char *pBackupName = NULL;
	UTF16ToEstring(SE_BACKUP_NAME, 0, &pBackupName);

	// The SeBackupPrivilege privilege bypasses regular ACLs for read purposes.
	success = ModifyPrivilege(pBackupName, !!enable);

	estrDestroy(&pBackupName);

	// For this to work, we must also open files with backup semantics.
	if (success)
		file_windows_backup_semantics = !!enable;

	return success;
}
