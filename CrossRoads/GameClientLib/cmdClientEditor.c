/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef NO_EDITORS

#include "EditorManager.h"
#include "file.h"
#include "oldencounter_common.h"


// Unload encounters on the client, so they're not drawn
AUTO_COMMAND ACMD_NAME(UnloadEncountersOnClient);
void encountersystem_UnloadEncountersOnClient(void)
{
	oldencounter_UnloadLayers();
}

// Opens the file, reads each line, and tries to load that piece of data into the editor using
//  the given type.
AUTO_COMMAND;
void EditorOpenFileList(const char *pchFilename, const char *pchType)
{
	FILE *pFile;
	char achBuf[1000];

	pFile = fileOpen(pchFilename, "rt");
	if (!pFile)
	{
		return;
	}

	// Force open the editor
	emOpenFileEx(NULL,pchType);

	while (fgets(achBuf, sizeof(achBuf), pFile))
	{
		char *pchStart = achBuf;
		char *pchIter;
		if (!pchStart || !*pchStart ||
			strStartsWith(pchStart, "//") ||
			strStartsWith(pchStart, "#") ||
			strStartsWith(pchStart, ";"))
			continue;

		for (pchIter = pchStart + strlen(pchStart) - 1; pchIter >= pchStart; pchIter--)
		{
			if (*pchIter == '\n')
				*pchIter = '\0';
			else
				break;
		}

		emOpenFileEx(achBuf,pchType);
	}

	fclose(pFile);
}

#endif