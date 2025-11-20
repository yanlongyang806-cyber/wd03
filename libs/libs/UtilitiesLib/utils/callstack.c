#include "callstack.h"
#include "estring.h"
#include "endian.h"
#include "timing.h"

#ifdef _XBOX
#include <xtl.h>
#include <xbdm.h>
#else
#include <guiddef.h>
#endif

#include "callstack_h_ast.h"

// ------------------------------------------------------------------------------
// Constants and Parsing helper routines

const char *LineContentHeaders[LINECONTENTS_COUNT] = 
{
	"", // LINECONTENTS_IGNORE = 0,

	"Modules:",      // LINECONTENTS_MODULES_START,
	"Module: ",      // LINECONTENTS_MODULE_NAME,
	"  PDB : ",      // LINECONTENTS_MODULE_PDB,
	"  Base: ",      // LINECONTENTS_MODULE_BASE_ADDRESS,
	"  Size: ",      // LINECONTENTS_MODULE_SIZE,
	"  Time: ",      // LINECONTENTS_MODULE_TIME,
	"  GUID: ",      // LINECONTENTS_MODULE_GUID,
	"  Age : ",      // LINECONTENTS_MODULE_AGE,
	"  Mthd: ",      // LINECONTENTS_MODULE_METHOD,
	"End Modules",   // LINECONTENTS_MODULES_END,

	"Callstack:",    // LINECONTENTS_CALLSTACK_START,
	"\t",            // LINECONTENTS_CALLSTACK_ADDRESS,
	"End Callstack", // LINECONTENTS_CALLSTACK_END,
};

static LineContents parseLine(char *pLine, char **pOutput)
{
	int i;

	if(pLine[0] == 0)
	{
		// Early out
		return LINECONTENTS_IGNORE;
	}

	for(i=1; i<LINECONTENTS_COUNT; i++) // Start with 1 to skip _IGNORE
	{
		if(strStartsWith(pLine, LineContentHeaders[i]))
		{
			*pOutput = (pLine + (int)strlen(LineContentHeaders[i]));
			return (LineContents)i;
		}
	}

	return LINECONTENTS_IGNORE;
}

// ------------------------------------------------------------------------------
// Report generation

bool callstackWriteTextReport(char *s, int iMaxLength)
{
#ifdef _XBOX
	// ------------------------------------------------------------------------------
	// Xbox 360 callstack query

	// Generic vars
	HRESULT hResult;

	// Stack query vars
	void* backTrace[32];
	int numEntries = 0;
	char buffer[256];
	int crypticTimeStamp;
	int i;
	
	// Module walking vars
	PDM_WALK_MODULES pWalkMod = NULL;
	DMN_MODLOAD modLoad;

	s[0] = 0;

    // Capture a stack back trace.
    hResult = DmCaptureStackBackTrace( ARRAY_SIZE(backTrace), backTrace );
	if(hResult != XBDM_NOERR)
	{
		return false;
	}

	// Count up our entries
	for(i = 0; i < ARRAY_SIZE(backTrace) && backTrace[i]; i++)
	{
		numEntries = i;
	}

	// Walk the list of loaded code modules.
	sprintf(buffer, "%s\n\n", LineContentHeaders[LINECONTENTS_MODULES_START]);
	strcat_s(s, iMaxLength, buffer);

	while( XBDM_NOERR == (hResult = DmWalkLoadedModules(&pWalkMod, &modLoad)) )
	{
		// Find the signature that describes the PDB file of the current module.
		DM_PDB_SIGNATURE signature = {0};
		DmFindPdbSignature( modLoad.BaseAddress, &signature );

		// Reverse a few pieces of the GUID so it prints properly
		signature.Guid.Data1 = endianSwapU32(signature.Guid.Data1);
		signature.Guid.Data2 = endianSwapU16(signature.Guid.Data2);
		signature.Guid.Data3 = endianSwapU16(signature.Guid.Data3);
		signature.Age        = endianSwapU32(signature.Age);

		crypticTimeStamp = timeGetSecondsSince2000FromWindowsTime32(modLoad.TimeStamp);

		// The upcoming code could be written in a prettier way. Oh well.

		sprintf(buffer, "%s%s\n", 
			LineContentHeaders[LINECONTENTS_MODULE_NAME],
			modLoad.Name);
		strcat_s(s, iMaxLength, buffer);

		sprintf(buffer, "%s%s\n", 
			LineContentHeaders[LINECONTENTS_MODULE_PDB],
			signature.Path);
		strcat_s(s, iMaxLength, buffer);

		sprintf(buffer, "%s%x\n", 
			LineContentHeaders[LINECONTENTS_MODULE_BASE_ADDRESS],
			modLoad.BaseAddress);
		strcat_s(s, iMaxLength, buffer);

		sprintf(buffer, "%s%08x\n", 
			LineContentHeaders[LINECONTENTS_MODULE_SIZE],
			modLoad.Size);
		strcat_s(s, iMaxLength, buffer);

		sprintf(buffer, "%s%d\n", 
			LineContentHeaders[LINECONTENTS_MODULE_TIME],
			crypticTimeStamp);
		strcat_s(s, iMaxLength, buffer);

		sprintf(buffer, "%s{%08X-%04X-%04X-%02X %02X-%02X %02X %02X %02X %02X %02X}\n", 
			LineContentHeaders[LINECONTENTS_MODULE_GUID],
			signature.Guid.Data1,
			signature.Guid.Data2,
			signature.Guid.Data3,
			signature.Guid.Data4[0], signature.Guid.Data4[1],
			signature.Guid.Data4[2], signature.Guid.Data4[3], 
			signature.Guid.Data4[4], signature.Guid.Data4[5], 
			signature.Guid.Data4[6], signature.Guid.Data4[7]);
		strcat_s(s, iMaxLength, buffer);

		sprintf(buffer, "%s%d\n\n", 
			LineContentHeaders[LINECONTENTS_MODULE_AGE],
			signature.Age);
		strcat_s(s, iMaxLength, buffer);
	}

	sprintf(buffer, "%s\n\n", LineContentHeaders[LINECONTENTS_MODULES_END]);
	strcat_s(s, iMaxLength, buffer);

	if(hResult != XBDM_ENDOFLIST)
	{
		return false;
	}
	DmCloseLoadedModules( pWalkMod );

	sprintf(buffer, "%s\n\n", LineContentHeaders[LINECONTENTS_CALLSTACK_START]);
	strcat_s(s, iMaxLength, buffer);

	sprintf(buffer, "Callstack: %d\n", numEntries);              strcat_s(s, iMaxLength, buffer);
    for(i = 0; i < numEntries; ++i )
	{
		sprintf(buffer, "%s%08x\n", LineContentHeaders[LINECONTENTS_CALLSTACK_ADDRESS], backTrace[i]);
		strcat_s(s, iMaxLength, buffer);
	}

	sprintf(buffer, "%s\n\n", LineContentHeaders[LINECONTENTS_CALLSTACK_END]);
	strcat_s(s, iMaxLength, buffer);

	return true;

#else
	// ------------------------------------------------------------------------------
	// PC Version, currently unsupported

	s[0] = 0;
	return false;
#endif
}

// ------------------------------------------------------------------------------
// GUID helper functions

bool GUIDFromString(const char *pStr, GUID *pGUID)
{
	return false;
}

// ------------------------------------------------------------------------------
// Temporary module information storage

// ------------------------------------------------------------------------------
// Callstack creation

CallStack * callstackCreateFromTextReport(const char *pReport)
{
#if PLATFORM_CONSOLE
	// ------------------------------------------------------------------------------
	// Xbox 360 Version, currently unsupported

	return NULL;
#else
	// ------------------------------------------------------------------------------
	// PC Version
	CallStack *pCallStack = StructCreate(parse_CallStack);
	CallStackModule * pCurrentModule = NULL;
	CallStackEntry *pCurrentEntry = NULL;

	char *pLine = NULL;
	char *pLineContext = NULL;
	char *pLineBuffer = NULL;
	char *pLineValue = NULL;

	// Duplicate this string on the stack so that we can abuse it with strtok_s()
	estrAllocaCreate(&pLineBuffer, (int)strlen(pReport)+1);
	estrCopy2(&pLineBuffer, pReport);

	pLine = strtok_s(pLineBuffer, "\n", &pLineContext);
	while(pLine != NULL)
	{
		LineContents eContents = parseLine(pLine, &pLineValue);
		switch(eContents)
		{
		case LINECONTENTS_MODULES_START:
			{
				//printf("LINECONTENTS_MODULE_START\n");
				break;
			}

		case LINECONTENTS_MODULE_NAME:
			{
				//printf("LINECONTENTS_MODULE_NAME '%s'\n", pLineValue);

				pCurrentModule = StructCreate(parse_CallStackModule);
				pCurrentModule->pModuleName = strdup(pLineValue);
				eaPush(&pCallStack->ppModules, pCurrentModule);
				break;
			}

		case LINECONTENTS_MODULE_PDB:
			{
				//printf("LINECONTENTS_MODULE_PDB '%s'\n", pLineValue);
				if(pCurrentModule)
				{
					pCurrentModule->pPDBName = strdup(pLineValue);
				}
				break;
			}

		case LINECONTENTS_MODULE_BASE_ADDRESS:
			{
				//printf("LINECONTENTS_MODULE_BASE_ADDRESS '%s'\n", pLineValue);
				if(pCurrentModule)
				{
					sscanf(pLineValue, "%x", &pCurrentModule->uBaseAddress);
				}
				break;
			}

		case LINECONTENTS_MODULE_SIZE:
			{
				//printf("LINECONTENTS_MODULE_SIZE '%s'\n", pLineValue);
				if(pCurrentModule)
				{
					sscanf(pLineValue, "%x", &pCurrentModule->uSize);
				}
				break;
			}

		case LINECONTENTS_MODULE_TIME:
			{
				//printf("LINECONTENTS_MODULE_TIME '%s'\n", pLineValue);
				if(pCurrentModule)
				{
					sscanf(pLineValue, "%d", &pCurrentModule->uTime);
				}
				break;
			}

		case LINECONTENTS_MODULE_GUID:
			{
				//printf("LINECONTENTS_MODULE_GUID '%s'\n", pLineValue);
				if(pCurrentModule)
				{
					pCurrentModule->pGuid = strdup(pLineValue);
				}
				break;
			}

		case LINECONTENTS_MODULE_AGE:
			{
				//printf("LINECONTENTS_MODULE_AGE '%s'\n", pLineValue);
				if(pCurrentModule)
				{
					sscanf(pLineValue, "%d", &pCurrentModule->uAge);
				}
				break;
			}

		case LINECONTENTS_MODULES_END:
			{
				//printf("LINECONTENTS_MODULES_END\n");
				break;
			}

		case LINECONTENTS_CALLSTACK_START:
			{
				//printf("LINECONTENTS_CALLSTACK_START\n");
				break;
			}

		case LINECONTENTS_CALLSTACK_ADDRESS:
			{
				//printf("LINECONTENTS_CALLSTACK_ADDRESS '%s'\n", pLineValue);
				pCurrentEntry = StructCreate(parse_CallStackEntry);
				sscanf(pLineValue, "%x", &pCurrentEntry->uAddress);
				eaPush(&pCallStack->ppEntries, pCurrentEntry);
				break;
			}

		case LINECONTENTS_CALLSTACK_END:
			{
				//printf("LINECONTENTS_CALLSTACK_END\n");
				break;
			}

		case LINECONTENTS_IGNORE:
		default:
			break;
		};

		pLine = strtok_s(NULL, "\n", &pLineContext);
	}

	estrDestroy(&pLineBuffer);
	return pCallStack;
#endif
}

#include "callstack_h_ast.c"
