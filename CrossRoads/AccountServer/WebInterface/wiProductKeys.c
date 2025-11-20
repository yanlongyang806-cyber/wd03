#include "wiProductKeys.h"
#include "wiCommon.h"
#include "timing.h"
#include "StringUtil.h"

/************************************************************************/
/* Index                                                                */
/************************************************************************/

static void wiHandleProductKeysIndex(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* View                                                                 */
/************************************************************************/

static void wiHandleProductKeysView(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* List                                                                 */
/************************************************************************/

static void wiHandleProductKeysList(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Create                                                               */
/************************************************************************/

static void wiHandleProductKeysCreate(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Handler                                                              */
/************************************************************************/

bool wiHandleProductKeys(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	bool bHandled = false;

	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

#define WI_PRODUCTKEY_PAGE(page) \
	if (!stricmp_safe(wiGetPath(pWebRequest), WI_PRODUCTKEYS_DIR #page WI_EXTENSION)) \
	{ \
		wiHandleProductKeys##page(pWebRequest); \
		bHandled = true; \
	}

	WI_PRODUCTKEY_PAGE(Index);
	WI_PRODUCTKEY_PAGE(View);
	WI_PRODUCTKEY_PAGE(List);
	WI_PRODUCTKEY_PAGE(Create);

#undef WI_PRODUCTKEY_PAGE

	PERFINFO_AUTO_STOP_FUNC();

	return bHandled;
}