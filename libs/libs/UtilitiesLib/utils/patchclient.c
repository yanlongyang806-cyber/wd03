#include "file.h"
#include "patchclient.h"
#include "GlobalTypes.h"

#define DEFAULT_PATCHCLIENT_OPTIONS "-skipselfpatch"

// Allow the full path of patchclient to be overriden.
static char gsPatchClientFullPath[MAX_PATH];
AUTO_CMD_STRING(gsPatchClientFullPath, setPatchClient) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

// Allow the full path of patchclient to be overriden.
static char gsPatchClientFullPath64[MAX_PATH];
AUTO_CMD_STRING(gsPatchClientFullPath64, setPatchClient64) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

// Return a pointer to the command line that should be used to run patchclient.
const char *patchclientCmdLine(bool bit64)
{
	return patchclientCmdLineEx(bit64, NULL, NULL);
}

// Like patchclientCmdLine(), but pass in some places to look for patchclient.
const char *patchclientCmdLineEx(bool bit64, const char *executable_directory, const char *core_executable_directory)
{
	static char result[CRYPTIC_MAX_PATH];
	sprintf(result, "%s " DEFAULT_PATCHCLIENT_OPTIONS, patchclientFullPathEx(bit64, executable_directory, core_executable_directory));
	return result;
}

// Return a pointer to the full path of patchclient.exe or patchclientx64.exe that goes with the current build.
const char *patchclientFullPath(bool bit64)
{
	return patchclientCmdLineEx(bit64, NULL, NULL);
}

// Like patchclientFullPath(), but pass in some places to look for patchclient.
const char *patchclientFullPathEx(bool bit64, const char *executable_directory, const char *core_executable_directory)
{
	const char *filename;
	static char result[CRYPTIC_MAX_PATH];

	// Check if the full client path has been overridden.
	if (bit64 && *gsPatchClientFullPath64)
		return gsPatchClientFullPath64;
	else if (!bit64 && *gsPatchClientFullPath)
		return gsPatchClientFullPath;

	// Get the filename to look for.
	filename = patchclientFilename(bit64);

	// In development mode, check for patchclient in places it would have been compiled into
	// or placed into by the continuous builder.
	if (isDevelopmentMode() && executable_directory)
	{
		sprintf(result, "%s/../../Utilities/bin/%s", executable_directory, filename);
		if (fileExists(result))
			return result;
	}

	// Otherwise, use the one in the core executables directory.
	if (core_executable_directory)
	{
		sprintf(result, "%s/%s", core_executable_directory, filename);
		if (fileExists(result))
			return result;
	}

	// If that didn't work, and we're some sort of dev tool, it's acceptable to use the one in c:\cryptic\tools.
	// Note: Never return a patchclient in c:\cryptic for anything that could be used in production, even if in dev mode.
	// The only patchclient used should be one from the current build.  If it can't be found, patchclient should be considered unavailable.
	if (!IsServer() && !IsClient())
	{
		sprintf(result, "%s/%s", fileCrypticToolsBinDir(), filename);
		if (fileExists(result))
			return result;
		sprintf(result, "c:/Night/tools/bin/%s", filename);
		if (fileExists(result))
			return result;
	}

	// If it isn't found anywhere, return null.
	return NULL;
}

// Return a pointer to the filename that patchclient is known by.
const char *patchclientFilename(bool bit64)
{
	if (bit64)
		return "patchclientx64.exe";
	else
		return "patchclient.exe";
}

// Return the "-SetPatchClient dir/patchclient.exe" option used to set the patchclient for a child process.
const char *patchclientParameter(bool bit64)
{
	static char result[CRYPTIC_MAX_PATH * 2];
	const char *patchclient = patchclientFullPath(bit64);
	if (!patchclient)
		return "";
	sprintf(result, "%s%s",
		patchclient ? " -SetPatchClient " : "",
		patchclient ? patchclient : "");
	return result;
}

// Return the "-SetPatchClient dir/patchclient.exe" option used to set the patchclient for a child process, calling patchclientFullPathEx().
const char *patchclientParameterEx(bool bit64, const char *executable_directory, const char *core_executable_directory)
{
	static char result[CRYPTIC_MAX_PATH * 2];
	const char *patchclient = patchclientFullPathEx(bit64, executable_directory, core_executable_directory);
	if (!patchclient)
		return "";
	sprintf(result, "%s%s",
		patchclient ? " -SetPatchClient " : "",
		patchclient ? patchclient : "");
	return result;
}
