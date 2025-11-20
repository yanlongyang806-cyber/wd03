#pragma once

void		regSetAppName(const char* appName);
const char* regGetAppName(void);
bool		regAppNameSet(void);
void		regRefresh(void);
const char* regGetCurrentVersion(const char* projectName);
const char* regGetLastVersion(const char* projectName);
void		regSetInstallationDir(const char* installDir);
const char* regGetInstallationDir(void);
const char* regGetPatchValue(void);
const char* regGetPatchBandwidth(void);
const char* regGetAppKey(void);

char*		regGetAppString(const char *key, const char *deflt, char *dest, size_t dest_size);
void		regPutAppString(const char *key, const char *value);
int			regGetAppInt(const char *key, int deflt);
void		regPutAppInt(const char *key, int value);
void		regDelete(const char *key);

char *regGetAppString_ForceAppName(const char *pAppName, const char *key, const char *deflt, char *dest, size_t dest_size);
void regPutAppString_ForceAppName(const char *pAppName, const char *key, const char *value);

char *regGetCrypticString(const char *key, const char *deflt, char *dest, size_t dest_size);
void regPutCrypticString(const char *key, const char *value);
int regGetCrypticInt(const char *key, int deflt);

typedef enum MachineMode
{
	MACHINEMODE_NONE,
	MACHINEMODE_PROGRAMMER,
	MACHINEMODE_PRODUCTION,
} MachineMode;

MachineMode	regGetMachineMode(void);

typedef enum RegWaitForDebugger
{
	REGWAITFORDEBUGGER_NONE,						// WaitForDebugger not found in registry
	REGWAITFORDEBUGGER_WAITFORDEBUGGER,				// WaitForDebugger found in registry
	REGWAITFORDEBUGGER_WAITFORDEBUGGER_BREAK,		// WaitForDebugger found in registry, with WaitForDebugger_Break
} RegWaitForDebugger;

// Check for WaitForDebugger request in registry.
RegWaitForDebugger regGetWaitForDebugger(void);
