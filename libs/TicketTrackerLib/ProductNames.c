#include "ProductNames.h"
#include "ProductNames_h_ast.h"

#include "file.h"
#include "fileutil.h"
#include "foldercache.h"
#include "objpath.h"
#include "error.h"
#include "Message.h"
#include "textparser.h"
#include "textparserinheritance.h"
#include "textparserutils.h"
#include "TicketAPI.h"
#include "estring.h"

static char **sppProductNames = NULL;
static char * sProductNameFile = "C:\\Core\\data\\ui\\messages\\all.productnames";

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_NameAndIdList* TT_GetProductsList()
{
	TT_NameAndIdList *list = StructCreate(parse_TT_NameAndIdList);
	int i;
	for (i=eaSize(&sppProductNames)-1; i>=0; i--)
	{
		TT_NameAndId *p = StructCreate(parse_TT_NameAndId);
		estrCopy2(&p->name, sppProductNames[i]);
		p->id = i;
		eaPush(&list->list, p);
	}
	return list;
}

const char *** productNamesGetEArray(void)
{
	ProductNamesList list;
	if (!sppProductNames)
	{
		ParserReadTextFile(sProductNameFile, parse_ProductNamesList, &list, 0);
		sppProductNames = list.ppProductNames;
	}
	return &sppProductNames;
}

int productNameGetIndex(const char *pProductName)
{
	int i;
	if (!pProductName)
		return -1;
	for (i=eaSize(&sppProductNames)-1; i>=0; i--)
	{
		if (stricmp(pProductName, sppProductNames[i]) == 0)
			return i;
	}
	return -1;
}

const char * productNameGetString(int index)
{
	if (index >= 0 && index < eaSize(&sppProductNames))
	{
		return sppProductNames[index];
	}
	return NULL;
}


static void productNameReloadCallback(const char *relpath, int when)
{
	ProductNamesList list = {0};
	loadstart_printf("Reloading Product Names...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	if(!ParserLoadFiles("ui", ".productnames", "ProductNames.bin", 0, parse_ProductNamesList, &list))
	{
		ErrorFilenamef(relpath, "Error reloading product names file: %s", relpath);
		eaDestroy(&list.ppProductNames);
	}
	else
	{
		eaDestroy(&sppProductNames);
		sppProductNames = list.ppProductNames;
	}

	loadend_printf("done");
}

AUTO_STARTUP(Category) ASTRT_DEPS(AS_Messages);
int productNamesLoadAll(void)
{
	static bool loadedOnce = false;
	ProductNamesList list = {0};
	int result;
	
	if (loadedOnce)
		return 1;

	result = ParserLoadFiles("ui", ".productnames", "ProductNames.bin", 0, parse_ProductNamesList, &list);

	if (!result)
	{
		eaDestroy(&list.ppProductNames);
		return 0;
	}
	sppProductNames = list.ppProductNames;

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/*.productnames", productNameReloadCallback);
	}
	loadedOnce = true;

	return 1;
}

const char *productNameGetDisplayName(const char *pProductName)
{
	static char sProductBuffer[128];
	sprintf(sProductBuffer, "ProductName_%s", pProductName);
	return TranslateMessageKey(sProductBuffer);
}

const char *productNameGetShortDisplayName(const char *pProductName)
{
	static char sProductBuffer[128];
	sprintf(sProductBuffer, "ProductNameShort_%s", pProductName);
	return TranslateMessageKey(sProductBuffer);
}

#include "ProductNames_h_ast.c"