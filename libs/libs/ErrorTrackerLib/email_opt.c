#include "email.h"
#include "earray.h"
#include "gimmeDLLWrapper.h"

#include "ETCommon/ETShared.h"

enum
{
	USER_UNKNOWN,
	USER_PROGRAMMER,
	USER_OTHER,
};

static int GetUserTypeFromName(const char *pUserName)
{
	const char *const *groups = NULL;
	int i;

	if (!gimmeDLLQueryExists())
		return USER_UNKNOWN;
	else
		groups = gimmeDLLQueryGroupListForUser(pUserName);

	for (i=0; i < eaSize(&groups); i++)
	{
		if (stricmp(groups[i], "Software") == 0)
		{
			return USER_PROGRAMMER;
		}
	}

	return USER_OTHER;
}

bool ArtistGotIt(ErrorEntry *p)
{
	int i;

	bool bAtLeastOneUnknownUser = false;

	for (i=0; i <eaSize(&p->ppUserInfo); i++)
	{
		if (p->ppUserInfo[i]->pUserName)
		{
			const char *pLastColon = strrchr(p->ppUserInfo[i]->pUserName, ':'); // find last colon
			const char *pUserNameWithoutPrefix = pLastColon ? (pLastColon+1) : p->ppUserInfo[i]->pUserName;
			switch (GetUserTypeFromName(pUserNameWithoutPrefix))
			{
			case USER_UNKNOWN:
				bAtLeastOneUnknownUser = true;
				break;
			case USER_PROGRAMMER:
				break;
			case USER_OTHER:
				return true;
			}
		}
	}

	if (!bAtLeastOneUnknownUser)
	{
		return false;
	}

	if(p->bProductionMode || RunningFromToolsBin(p))
	{
		return true;
	}

	return false;
}
