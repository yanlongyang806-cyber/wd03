#include "ETCommon/ETCommonStructs.h"
#include "ETCommon/ETIncomingData.h"
#include "ETCommon/ETWebCommon.h"
#include "AutoGen/ETCommonStructs_h_ast.h"
#include "AutoGen/ETCommonStructs_c_ast.h"

#include "jira.h"
#include "GlobalTypes.h"
#include "StructInit.h"
#include "StashTable.h"
#include "error.h"
#include "trivia.h"
#include "AutoGen/trivia_h_ast.h"

bool gbETVerbose = false;
AUTO_CMD_INT(gbETVerbose, ETVerbose) ACMD_CMDLINE;
int gMaxIncomingDumps = 0;
AUTO_CMD_INT(gMaxIncomingDumps, MaxIncomingDumps) ACMD_CMDLINE;
int gMaxIncomingDumpsPerEntry = 0;
AUTO_CMD_INT(gMaxIncomingDumpsPerEntry, MaxIncomingDumpsPerEntry) ACMD_CMDLINE;
int gUseRemoteSymServ = 0;
AUTO_CMD_INT(gUseRemoteSymServ, UseRemoteSymServ) ACMD_CMDLINE;
bool gIgnoreCallocTimed = false;
AUTO_CMD_INT(gIgnoreCallocTimed, IgnoreCallocTimed) ACMD_CMDLINE;
char gTempPDBDirectory[MAX_PATH] = "";
AUTO_CMD_STRING(gTempPDBDirectory, pdbdir) ACMD_CMDLINE;


AUTO_COMMAND;
void setMaxIncomingDumps(int max)
{
	gMaxIncomingDumps = max;
	if(max)
		printf("Max Incoming Dumps: %d\n", gMaxIncomingDumps);
	else
		printf("Max Incoming Dumps: Disabled\n");
}

AUTO_COMMAND;
void setMaxIncomingDumpsPerEntry(int max)
{
	gMaxIncomingDumpsPerEntry = max;
	if(max)
		printf("Max Incoming Dumps Per Entry: %d\n", gMaxIncomingDumpsPerEntry);
	else
		printf("Max Incoming Dumps Per Entry: Disabled\n");
}

ErrorTrackerSettings gErrorTrackerSettings = {0};
extern StashTable dumpIDToDumpDataTable;
extern StashTable errorSourceFileLineTable;

//Cryptic-owned DLLs, for symbol lookup checking - these should always resolve.
char *crypticDLLs[] = {
	"nvtt.dll",
	"NxCooking.dll",
	"NxCookingDEBUG.dll",
	"PhysXCore.dll",
	"PhysXCoreDEBUG.dll",
	"PhysXLoader.dll",
	"PhysXLoaderDEBUG.dll",
	"XWrapper.dll",
};

bool isCrypticModule(const char *module)
{
	if (!module)
		return false;
	if (strstr(module, ".exe"))
	{
		return true;
	}
	else
	{
		U32 i;
		for (i = 0; i < sizeof(crypticDLLs) / sizeof(*crypticDLLs); i++)
		{
			if (strstr(module, crypticDLLs[i]))
			{
				return true;
			}
		}
	}
	return false;
}


static U32 suOptions = 0;
U32 errorTrackerLibGetOptions(void)
{
	return suOptions;
}
void errorTrackerLibSetOptions(U32 uOptions)
{
	suOptions = uOptions;
}

AUTO_RUN;
void ETCommonAutoInit(void)
{
	StructInit(parse_ErrorTrackerSettings, &gErrorTrackerSettings);
}
void ETCommonInit(void)
{
	dumpIDToDumpDataTable = stashTableCreateInt(64);
	errorSourceFileLineTable = stashTableCreateWithStringKeys(512, StashDeepCopyKeys_NeverRelease);

	loadstart_printf("Opening incoming data port... ");
	initIncomingData();
	initIncomingPublicData();
	loadend_printf("Now accepting errors.");

	loadstart_printf("Opening incoming secure data port... ");
	initIncomingSecureData();
	loadend_printf("Now accepting errors on secure port.");

	loadstart_printf("Opening web interface...      ");
	ETWeb_Init();
	loadend_printf("Now accepting requests.");
}

AUTO_STRUCT;
typedef struct ETSVNProductMap
{
	const char *key; AST(KEY UNOWNED)
	const char *value; AST(UNOWNED)
} ETSVNProductMap;

static EARRAY_OF(ETSVNProductMap) sProductFullToShort = NULL;
static EARRAY_OF(ETSVNProductMap) sProductShortToFull = NULL;

const char *ETGetShortProductName (const char *fullName)
{
	ETSVNProductMap *found;
	if (!sProductFullToShort)
		return NULL;
	found = eaIndexedGetUsingString(&sProductFullToShort, fullName);
	if (!found)
		return NULL;
	return found->value;
}

const char *ETGetFullProductName (const char *shortName)
{
	ETSVNProductMap *found;
	if (!sProductShortToFull)
		return NULL;
	found = eaIndexedGetUsingString(&sProductShortToFull, shortName);
	if (!found)
		return NULL;
	return found->value;
}

// To be called whenever gErrorTrackerSettings is reloaded
void ETReloadSVNProductNameMappings(void)
{
	int size, i;
	ETSVNProductMap *newFullMap, *newShortMap;
	
	eaDestroyStruct(&sProductFullToShort, parse_ETSVNProductMap);
	eaIndexedEnable(&sProductFullToShort, parse_ETSVNProductMap);

	eaDestroyStruct(&sProductShortToFull, parse_ETSVNProductMap);
	eaIndexedEnable(&sProductShortToFull, parse_ETSVNProductMap);

	size = eaSize(&gErrorTrackerSettings.eaSVNProductMappings);
	for (i=0; i<size; i+=2)
	{
		const char *shortName = gErrorTrackerSettings.eaSVNProductMappings[i];
		const char *fullName = gErrorTrackerSettings.eaSVNProductMappings[i+1];
		if (i == size - 1) // Odd number of entries shouldn't happen
			break;
		newFullMap = StructCreate(parse_ETSVNProductMap);
		newShortMap = StructCreate(parse_ETSVNProductMap);
		newFullMap->key = newShortMap->value = fullName;
		newFullMap->value = newShortMap->key = shortName;
		eaIndexedAdd(&sProductFullToShort, newFullMap);
		eaIndexedAdd(&sProductShortToFull, newShortMap);
	}
}

#include "AutoGen/ETCommonStructs_h_ast.c"
#include "AutoGen/ETCommonStructs_c_ast.c"