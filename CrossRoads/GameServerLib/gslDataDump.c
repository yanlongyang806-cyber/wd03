#include "gslDataDump.h"
#include "itemCommon.h"
#include "resourceInfo.h"
#include "file.h"
#include "Message.h"

bool gbDataDumpEnable = false;
AUTO_CMD_INT(gbDataDumpEnable, DataDump);

// Dump game data to flat files for use in the reporting database
void gslDataDump(void)
{

	ItemDef *pItemDef;
	ResourceIterator resIterator;
	char outPath[MAX_PATH], buf[1024];
	FILE *out;

	if(!gbDataDumpEnable)
		return;

	fileLocateWrite("csv/itemsdef.csv", outPath);
	makeDirectoriesForFile(outPath);
	out = fopen(outPath, "w");
	sprintf(buf, "id,name\n");
	fwrite(buf, strlen(buf), 1, out);
	resInitIterator(g_hItemDict, &resIterator);
	while (resIteratorGetNext(&resIterator, NULL, &pItemDef))
	{
		sprintf(buf, "%s,%s\n", pItemDef->pchName, langTranslateMessageRef(LANGUAGE_ENGLISH, pItemDef->displayNameMsg.hMessage));
		fwrite(buf, strlen(buf), 1, out);
	}
	resFreeIterator(&resIterator);
	fclose(out);
}