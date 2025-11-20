#include "aslLoginTokenParsing.h"
#include "timing.h"
#include "FolderCache.h"
#include "earray.h"
#include "aslLoginServer.h"
#include "utils.h"
#include "fileutil2.h"
#include "Message.h"

#define PLAYTIME_FOLDER_NAME "playtimes"

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

typedef struct PlayTimeStruct 
{
	SYSTEMTIME flags, start, end;
} PlayTimeStruct;

typedef struct PlayTimeToken
{
	char *tokenString;
	PlayTimeStruct ** eaPlayTimes;
} PlayTimeToken;

static PlayTimeToken** seaPlayTimes = NULL;
static bool sbLoadedPlayTimes = false;

static void loadPlayTimeRestrictions(void);
static void loadPlayTimeRestrictionFile(SA_PARAM_NN_STR const char *relpath);

static PlayTimeToken * findPlayTimeToken(const char *token)
{
	int i, size = eaSize(&seaPlayTimes);
	if (!token)
		return NULL;
	for (i=0; i<size; i++)
	{
		if (stricmp(token, seaPlayTimes[i]->tokenString) == 0)
			return seaPlayTimes[i];
	}
	return NULL;
}

static void removePlayTimeRestrictions(const char *relpath, int when)
{
	if (strstri(relpath, PLAYTIME_FOLDER_NAME))
	{
		PlayTimeToken *token;
		char *filename = strstri(relpath, PLAYTIME_FOLDER_NAME)+1+strlen(PLAYTIME_FOLDER_NAME);
		char *tokenString = strdup(filename), *end;
		end = strstri(tokenString, ".txt");
		if (end) *end = 0;
		
		token = findPlayTimeToken(tokenString);
		free(tokenString);
		if (token)
		{
			free(token->tokenString);
			eaDestroyEx(&token->eaPlayTimes, freeWrapper);
		}
		eaFindAndRemove(&seaPlayTimes, token);
	}
}

static void reloadPlayTimeRestrictions(const char *relpath, int when)
{
	if (strstri(relpath, PLAYTIME_FOLDER_NAME))
	{
		char *filename = strstri(relpath, PLAYTIME_FOLDER_NAME)+1+strlen(PLAYTIME_FOLDER_NAME);
		loadPlayTimeRestrictionFile(filename);
	}
}

static void loadPlayTimeRestrictionFile(const char *relpath)
{
	char *s, *mem, *args[4];
	int	count;
	char filename[MAX_PATH];
	char *tokenString, *end;
	PlayTimeToken *token = NULL;
	bool bIsNewToken = false;

	if (strstri(relpath, PLAYTIME_FOLDER_NAME))
	{
		sprintf(filename, "%s", relpath);
		tokenString = strdup(strstri(relpath, PLAYTIME_FOLDER_NAME)+1+strlen(PLAYTIME_FOLDER_NAME));
	}
	else
	{
		sprintf(filename,"%s/%s/%s",fileLocalDataDir(), PLAYTIME_FOLDER_NAME, relpath);
		tokenString = strdup(relpath);
	}
	end = strstri(tokenString, ".txt");
	if (end) *end = 0;

	token = findPlayTimeToken(tokenString);
	if (token)
		eaDestroyEx(&token->eaPlayTimes, freeWrapper);
	else
	{
		token = calloc(1, sizeof(PlayTimeToken));
		token->tokenString = strdup(tokenString);
		bIsNewToken = true;
	}
	free(tokenString);
	
	mem = fileAlloc( filename, 0 );
	count = tokenize_line(mem,args,&s);
	while (count)
	{
		PlayTimeStruct *times = calloc(1, sizeof(PlayTimeStruct));
		parseFriendsAndFamilyTimeRestrictions (count, args, &times->flags, &times->start, &times->end);
		eaPush(&token->eaPlayTimes, times);
		count = tokenize_line(s, args, &s);
	}
	if (bIsNewToken)
		eaPush(&seaPlayTimes, token);
	free(mem);
}

static void loadPlayTimeRestrictions(void)
{
	char filename[MAX_PATH];

	sprintf(filename,"%s/%s",fileLocalDataDir(), PLAYTIME_FOLDER_NAME);

	if (dirExists(filename))
	{
		char ** files = fileScanDirNoSubdirRecurse(filename);
		int i, size = eaSize(&files);

		for (i=0; i<size; i++)
		{
			loadPlayTimeRestrictionFile(files[i]);
		}
		eaDestroyEx(&files, freeWrapper);
	}

	if (!sbLoadedPlayTimes)
	{
		char filepath[MAX_PATH];
		sprintf(filepath, "%s/*.txt", PLAYTIME_FOLDER_NAME);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, filepath, reloadPlayTimeRestrictions);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_DELETE, filepath, removePlayTimeRestrictions);
	}
	sbLoadedPlayTimes = true;
}

LATELINK;
bool aslProcessSpecialTokens(LoginLink *loginLink, CONST_STRING_EARRAY eaTokens, AccountPermissionStruct *pProductPermissions);

//jswdeprecated
bool DEFAULT_LATELINK_aslProcessSpecialTokens(LoginLink *loginLink, CONST_STRING_EARRAY eaTokens, AccountPermissionStruct *pProductPermissions)
{
	int i, size;
	bool bMatchedTime = false;
	bool bFoundTokenRestrictions = false;

	if (!sbLoadedPlayTimes)
		loadPlayTimeRestrictions();

	if (!loginLink)
		return false;
	if (!eaTokens)
		return true;
	size = eaSize(&eaTokens);
	if (size == 0) // if no token files are loaded, there are no restriction
		return true;
	
	for (i=0; i<size; i++)
	{
		PlayTimeToken *token = findPlayTimeToken(eaTokens[i]);
		if (token)
		{
			int j;
			U32 uTime = timeSecondsSince2000();

			bFoundTokenRestrictions = true;
			for (j=eaSize(&token->eaPlayTimes)-1; j>=0; j--)
			{
				if ( timeLocalIsInRange(uTime, &token->eaPlayTimes[j]->start, &token->eaPlayTimes[j]->end, 
					&token->eaPlayTimes[j]->flags) )
				{
					bMatchedTime= true;
					break;
				}
			}
		}
	}

	if (bFoundTokenRestrictions && !bMatchedTime)
	{
		if(pProductPermissions && permissionsGame(pProductPermissions, ACCOUNT_PERMISSION_UGC_ALLOWED))
		{
			loginLink->bUGCShardCharactersOnly = true;
		}
		else
		{
			aslFailLogin(loginLink, langTranslateMessageKey(loginLink->clientLangID, "LoginServer_NotAllowedRightNow"));
			return false;
		}
	}
	return true;
}

bool 
aslLogin2_DoPermissionTokensAllowPlay(CONST_STRING_EARRAY tokens)
{
    int i;
    bool matchedTime = false;
    bool foundTokenRestrictions = false;
    U32 currentTime;

    if (!sbLoadedPlayTimes)
    {
        loadPlayTimeRestrictions();
    }

    currentTime = timeSecondsSince2000();

    for ( i = eaSize(&tokens) - 1; i >= 0; i-- )
    {
        PlayTimeToken *token = findPlayTimeToken(tokens[i]);
        if (token)
        {
            int j;
            
            foundTokenRestrictions = true;
            for ( j = eaSize(&token->eaPlayTimes) - 1; j >= 0; j-- )
            {
                if ( timeLocalIsInRange(currentTime, &token->eaPlayTimes[j]->start, &token->eaPlayTimes[j]->end, &token->eaPlayTimes[j]->flags) )
                {
                    // Play time restrictions for the token allow play now.
                    return true;
                }
            }
        }
    }

    // Player can still play if there were no token restrictions.
    return !foundTokenRestrictions;
}