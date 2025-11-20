#include "wiSubscriptions.h"
#include "wiCommon.h"
#include "timing.h"
#include "StringUtil.h"

/************************************************************************/
/* Index                                                                */
/************************************************************************/

static void wiHandleSubscriptionsIndex(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* View                                                                 */
/************************************************************************/

static void wiHandleSubscriptionsView(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* List                                                                 */
/************************************************************************/

static void wiHandleSubscriptionsList(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Create                                                               */
/************************************************************************/

static void wiHandleSubscriptionsCreate(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Handler                                                              */
/************************************************************************/

bool wiHandleSubscriptions(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	bool bHandled = false;

	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

#define WI_SUBSCRIPTION_PAGE(page) \
	if (!stricmp_safe(wiGetPath(pWebRequest), WI_SUBSCRIPTIONS_DIR #page WI_EXTENSION)) \
	{ \
		wiHandleSubscriptions##page(pWebRequest); \
		bHandled = true; \
	}

	WI_SUBSCRIPTION_PAGE(Index);
	WI_SUBSCRIPTION_PAGE(View);
	WI_SUBSCRIPTION_PAGE(List);
	WI_SUBSCRIPTION_PAGE(Create);

#undef WI_SUBSCRIPTION_PAGE

	PERFINFO_AUTO_STOP_FUNC();

	return bHandled;
}