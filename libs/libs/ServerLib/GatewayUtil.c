/***************************************************************************
 
 
 ***************************************************************************/
#include "EString.h"
#include "file.h"
#include "sysutil.h"
#include "ServerLib.h"
#include "GlobalTypes.h"
#include "utils.h"

#include "GatewayUtil.h"

static int s_bControllerReportsGatewayLocked = false;
AUTO_CMD_INT(s_bControllerReportsGatewayLocked, ControllerReportsGatewayLocked) ACMD_COMMANDLINE;

// If true, uses the /debug version of Gateway, otherwise uses /prod.
static int s_bGatewayDebug = false;
AUTO_CMD_INT(s_bGatewayDebug, GatewayDebug) ACMD_COMMANDLINE;

// Sets the deploy directory for Gateway. (No trailing backslash. No debug/prod.)
static char *s_estrGatewayDeployDir = NULL;
AUTO_CMD_ESTRING(s_estrGatewayDeployDir, GatewayDeployDir) ACMD_COMMANDLINE;

AUTO_COMMAND_REMOTE;
void GatewayIsLocked(bool bLocked)
{
	s_bControllerReportsGatewayLocked = bLocked;

	GatewayNotifyLockStatus(gateway_IsLocked());
}

void DEFAULT_LATELINK_GatewayNotifyLockStatus(bool bLocked)
{
	// The user of this library is responsible for OVERRIDE-ing this function.
}


bool gateway_IsLocked(void)
{
	return ControllerReportsShardLocked() || s_bControllerReportsGatewayLocked;
}


static bool TryDir(char **pestr, const char *pchRoot, const char *pchPath)
{
	estrCopy2(pestr, pchRoot);
	estrAppend2(pestr, pchPath);
	estrAppend2(pestr, s_bGatewayDebug ? "/debug" : "/prod");

	estrAppend2(pestr, "/bin/node.exe");

	if(fileExists(*pestr))
	{
		estrCopy2(pestr, pchRoot);
		estrAppend2(pestr, pchPath);
		estrAppend2(pestr, s_bGatewayDebug ? "/debug" : "/prod");

		return true;
	}

	return false;
}

bool gateway_FindDeployDir(char **pestr)
{
	char achExeDir[MAX_PATH];

	getExecutableDir(achExeDir);

	// First look in the src tree, which is where it would be for a
	//   programmer.
	// Then in the data directory, which is where it would be for a normal
	//   developer.
	// Then in the gateway directory, which is where it should be for a 
	//   server shard.
	if((s_estrGatewayDeployDir ? TryDir(pestr, s_estrGatewayDeployDir, "") : false)
		|| TryDir(pestr, achExeDir, "/../../Gateway/build/deploy")
		|| TryDir(pestr, fileDataDir(), "/gateway")
		|| TryDir(pestr, achExeDir, "/gateway"))
	{
		backSlashes(*pestr);
		return true;
	}

	estrPrintf(pestr, "fileDataDir:%s, exedir: %s", fileDataDir(), achExeDir);

	return false;
}

bool gateway_FindScriptDir(char **pestr)
{
	if(gateway_FindDeployDir(pestr))
	{
		char *estrProductPath = NULL;
		estrCopy(&estrProductPath, pestr);
		estrAppend2(&estrProductPath, "/");
		estrAppend2(&estrProductPath, GetProductName());

		if(dirExists(estrProductPath))
		{
			estrCopy(pestr, &estrProductPath);
		}

		estrDestroy(&estrProductPath);

		backSlashes(*pestr);
		return true;
	}

	return false;
}

/* End of File */
