// Deprecated

/*#include "timing.h"

void parseFriendsAndFamilyTimeRestrictions(int count, char *args[], SYSTEMTIME *flags, SYSTEMTIME *start, SYSTEMTIME *end)
{
	int i;
	U8 uStartDay = 0;
	U8 uStartTime = 0;

	// reset all the values
	clearSystemTimeStruct(flags);
	
	for (i=0; i<count; i++)
	{
		if ('0' <= args[i][0] && args[i][0] <= '9')
		{
			// time
			if (strlen(args[i]) < 4)
				continue;
			if (!uStartTime)
			{
				uStartTime = 1;
				start->wHour = 10*(args[i][0] - '0') + args[i][1] - '0';
				start->wMinute = 10*(args[i][2] - '0') + args[i][3] - '0';
			}
			else
			{
				uStartTime = 0x11;
				end->wHour = 10*(args[i][0] - '0') + args[i][1] - '0';
				end->wMinute = 10*(args[i][2] - '0') + args[i][3] - '0';
			}
		}
		else
		{
			// day of week
			int dayOfWeek = -1;
			if (stricmp(args[i], "sunday") == 0)
				dayOfWeek = 0;
			else if (stricmp(args[i], "monday") == 0)
				dayOfWeek = 1;
			else if (stricmp(args[i], "tuesday") == 0)
				dayOfWeek = 2;
			else if (stricmp(args[i], "wednesday") == 0)
				dayOfWeek = 3;
			else if (stricmp(args[i], "thursday") == 0)
				dayOfWeek = 4;
			else if (stricmp(args[i], "friday") == 0)
				dayOfWeek = 5;
			else if (stricmp(args[i], "saturday") == 0)
				dayOfWeek = 6;

			if (!uStartDay && dayOfWeek >= 0)
			{
				start->wDayOfWeek = dayOfWeek;
				uStartDay = 1;
			}
			else if (dayOfWeek >= 0)
			{
				end->wDayOfWeek = dayOfWeek;
				uStartDay = 0x11;
			}
		}
	}
	if (uStartDay)
	{
		if (uStartDay == 1)
			end->wDayOfWeek = start->wDayOfWeek;
		flags->wDayOfWeek = 1;
	}
	if (uStartTime == 0x11)
	{
		flags->wHour = 1;
		flags->wMinute = 1;
	}	
}

typedef struct BetaTimeStruct 
{
	SYSTEMTIME flags, start, end;
} BetaTimeStruct;
typedef struct BetaTimeProductStruct
{
	char *pProductName;
	BetaTimeStruct **eaBetaTimes;
} BetaTimeProductStruct;
static BetaTimeProductStruct** seaBetaTimes = NULL;

static BetaTimeProductStruct* findBetaTimes(const char *pProductName)
{
	int i;
	for (i=eaSize(&seaBetaTimes)-1; i>=0; i--)
	{
		if (stricmp(pProductName, seaBetaTimes[i]->pProductName) == 0)
			return seaBetaTimes[i];
	}
	return NULL;
}

static BetaTimeProductStruct* loadBetaTimeRestrictions(const char *pProductName, BetaTimeProductStruct *product);
static void reloadBetaTimeRestrictions(const char *relpath, int when)
{
	char *delim = strchr(relpath, '/');
	if (delim)
	{
		BetaTimeProductStruct *product;
		*delim = 0;
		product = findBetaTimes(relpath);
		loadBetaTimeRestrictions(relpath, product);
	}
}
static BetaTimeProductStruct* loadBetaTimeRestrictions(const char *pProductName, BetaTimeProductStruct *product)
{
	static sbCallbackSet = false;
	char *s, *mem, *args[4];
	int	count;
	char filename[MAX_PATH];
	bool bMatchedTime = false;

	if (pProductName && *pProductName)
		sprintf_s(SAFESTR(filename),"%s/%s/%s",fileLocalDataDir(),pProductName,"ffbeta.txt");
	else
		sprintf_s(SAFESTR(filename),"%s/%s",fileLocalDataDir(),"ffbeta.txt");
	mem = fileAlloc( filename, 0 );

	if (!product)
	{
		product = calloc(1, sizeof(BetaTimeProductStruct));
		product->pProductName = pProductName ? strdup(pProductName) : strdup("");
		eaPush(&seaBetaTimes, product);
		
	}
	else
		eaDestroyEx(&product->eaBetaTimes, freeWrapper);

	count = tokenize_line(mem,args,&s);
	while (count)
	{
		BetaTimeStruct *times = calloc(1, sizeof(BetaTimeStruct));
		parseFriendsAndFamilyTimeRestrictions (count, args, &times->flags, &times->start, &times->end);
		eaPush(&product->eaBetaTimes, times);
		count = tokenize_line(s, args, &s);
	}
	free(mem);

	if (!sbCallbackSet)
	{
		sbCallbackSet = true;
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ffbeta.txt", reloadBetaTimeRestrictions);
	}
	return product;
}

bool ProcessSpecialTokenAccess(CONST_STRING_EARRAY eaTokens, const char *pProductName)
{
	int i, size = eaSize(&eaTokens);

	for (i=0; i<size; i++)
	{
		if (stricmp(eaTokens[i], "ffbeta") == 0)
		{
			int j;
			bool bMatchedTime = false;
			BetaTimeProductStruct* betaTimes = NULL;

			if (!(betaTimes = findBetaTimes(pProductName)))
				betaTimes = loadBetaTimeRestrictions(pProductName, NULL);

			for (j=eaSize(&betaTimes->eaBetaTimes)-1; j>=0; j--)
			{
				if ( timeLocalIsInRange(timeSecondsSince2000(), &betaTimes->eaBetaTimes[j]->start, 
					&betaTimes->eaBetaTimes[j]->end, &betaTimes->eaBetaTimes[j]->flags) )
				{
					bMatchedTime= true;
					break;
				}
			}

			if (!bMatchedTime)
			{
				return false;
			}
		}
	}
	return true;
}*/
