#include "crypt.h"
#include "patchcommonutils.h"
#include "file.h"
#include "utils.h"
#include "timing.h"
#include "sysutil.h"
#include "Alerts.h"
#include "UTF8.h"
#include "fileutil.h"

#if _PS3
#else
#include "windefinclude.h"
#endif


U32 patchChecksum(void *data,U32 len)
{
	PERFINFO_AUTO_START_FUNC();

	if(!data)
	{
		Errorf("Trying to run patchChecksum(NULL, %u)", len);
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}
	
	if(len)
	{
		U32	md5_buf[4];

		cryptMD5(data, len, md5_buf);
		PERFINFO_AUTO_STOP_FUNC();
		return md5_buf[0];
	}

	PERFINFO_AUTO_STOP_FUNC();
	return 0;
}

U32 patchChecksumFile(const char *file)
{
	char buf[1024];
	U32	md5_buf[4], l;
	size_t t=0;
	FILE *f;

	PERFINFO_AUTO_START_FUNC();

	f = fopen(file, "rb");
	if(!f)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}

	while(l = (U32)fread(buf, 1, ARRAY_SIZE_CHECKED(buf), f))
	{
		cryptMD5Update(buf, l);
		t += l;
	}
	fclose(f);
	cryptMD5Final(md5_buf);

	PERFINFO_AUTO_STOP_FUNC();

	if(t == 0)
		return 0; // NOTE: Hack to comply with patchChecksum return value for 0-byte file <NPK 2008-12-16>

	return md5_buf[0];

}

U32 getCurrentFileTime(void)
{
	return timeSecondsSince2000() + MAGIC_SS2000_TO_FILETIME;
// 	FILE	*file;
// 	U32		time;
// 	int unlink_ret;
// 
// 	file	= fopen("c:/timehack.txt","wb");
// 	assert(file);
// 	fclose(file);
// 	time	= fileLastChangedAltStat("c:/timehack.txt");
// 	unlink_ret = unlink("c:/timehack.txt");
// 	return time;
}


void fixPath_s(char * path, size_t path_size, const char * stripped)
{
	char * tok, * strtok_context = NULL, strtoken[MAX_PATH], path_rebuild[MAX_PATH];

	forwardSlashes(path);

	if(strnicmp("./", path, 2) == 0)
		memmove(path, path + 2, strlen(path + 2) + 1);

	tok = strchr(path, ':');
	if(tok)
	{
		tok += (tok[1] == '/') ? 2 : 1;
		memmove(path, tok, strlen(tok) + 1);
	}

	if(stripped)
	{
		path_rebuild[0] = 0;
		strcpy(strtoken, stripped);

		tok = strtok_s(strtoken, "/", &strtok_context);
		while(tok)
		{
			if(strnicmp(tok, path, strlen(tok)) == 0 && path[strlen(tok)] == '/')
				break;
			strcat(path_rebuild, tok);
			strcat(path_rebuild, "/");
			tok = strtok_s(NULL, "/", &strtok_context);
		}

		strcat(path_rebuild, path);
		strcpy_s(path, path_size, path_rebuild);
	}
}

void stripPath(char * path, const char * stripped)
{
	char * stripped_tok, * path_tok, stripped_copy[MAX_PATH], * stripped_context = NULL, path_copy[MAX_PATH], * path_context = NULL;
	int len;

	if(stripped)
	{
		strcpy(stripped_copy, stripped);
		strcpy(path_copy, path);

		forwardSlashes(stripped_copy);
		forwardSlashes(path_copy);

		stripped_tok = strtok_s(stripped_copy, "/", &stripped_context);
		path_tok = strtok_s(path_copy, "/", &path_context);

		while(stripped_tok && path_tok)
		{
			ANALYSIS_ASSUME(stripped_tok);
			ANALYSIS_ASSUME(path_tok);
			if(stricmp(stripped_tok, path_tok) == 0)
			{
				len = (int)strlen(path_tok) + 1;
				memmove(path, path + len, strlen(path + len) + 1);
			}
			stripped_tok = strtok_s(NULL, "/", &stripped_context);
			path_tok = strtok_s(NULL, "/", &path_context);
		}
	}
}

void machinePath_s(char *adjusted, size_t adjusted_size, const char *path)
{
	int absolute;

#if _XBOX // don't use relative paths
	while(path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
		path += 2;
	if(path[0] == '.' && path[1] == '\0')
		path++;
#endif

	absolute = fileIsAbsolutePath(path);

	// use a temporary buffer if necessary
	if( (!absolute || adjusted != path) &&
		path < adjusted + adjusted_size &&
		adjusted < path + strlen(path) )
	{
		char *temp = _alloca(adjusted_size);
		strcpy_s(temp, adjusted_size, path);
		path = temp;
	}

	if(absolute)
	{
		if(adjusted != path)
			strcpy_s(adjusted, adjusted_size, path);
	}
	else
	{
#if _PS3
        strcpy_s(adjusted, adjusted_size, "/app_home");
#elif _XBOX
		strcpy_s(adjusted, adjusted_size, "DEVKIT:\\fightclub"); // TODO: get rid of fightclub-only stuff
#else
		(void)fileGetcwd(adjusted,(int)adjusted_size);
		forwardSlashes(adjusted);
		if(adjusted[strlen(adjusted)-1] == '/')
			adjusted[strlen(adjusted)-1] = '\0';
#endif
		if(path[0])
		{
			strcat_s(adjusted, adjusted_size, "/");
			strcat_s(adjusted, adjusted_size, path);
		}
	}

#if _XBOX
	backSlashes(adjusted);
#endif
}

int patchRenameWithAlert_dbg(const char *source, const char *dest MEM_DBG_PARMS)
{
	int localerrno = rename(source, dest);
	if(localerrno != 0)
	{
		char *errorstring = NULL;
		char error[256] = {0};
		DWORD winerror = GetLastError();
		char *pWindowsErrorMessage = NULL;
		strerror_s(SAFESTR(error), localerrno);
		estrPrintf(&errorstring, "(%d: %s)", localerrno, error);

		if (!FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, winerror, 0, &pWindowsErrorMessage, NULL))
			estrPrintf(&pWindowsErrorMessage, "Error code: %d\n", winerror);

		estrAppend2(&errorstring, pWindowsErrorMessage);

		if (winerror == ERROR_SHARING_VIOLATION || winerror == ERROR_ACCESS_DENIED)
		{
			char shortname[MAX_PATH];
			getFileNameNoExt(shortname, source);
			
			RunHandleExeAndAlert("PATCHSERVER_RENAME_FAILED", shortname, "hogg_sharing", "Failed to rename %s to %s (%s:%d). %s", NULL_TO_EMPTY(source), NULL_TO_EMPTY(dest), caller_fname, line, errorstring);
		}
		else
		{
			TriggerAlertDeferred("PATCHSERVER_RENAME_FAILED", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 
				"Failed to rename %s to %s (%s:%d). %s", NULL_TO_EMPTY(source), NULL_TO_EMPTY(dest), caller_fname, line, errorstring);
		}

		estrDestroy(&errorstring);
		estrDestroy(&pWindowsErrorMessage);
	}
	
	return localerrno;
}

// Parse an HTTP info string.
bool patchParseHttpInfo(const char *http_info, char *server, size_t server_size, U16 *port, char *prefix, size_t prefix_size)
{
	unsigned long parse_port = 80;
	int matches;

	// If no info, fail.
	if (!http_info)
		return false;

	// Parse.
	server[0] = 0;
	prefix[0] = 0;
	matches = sscanf_s(http_info, "%[^:]:%d/%s", SAFESTR2(server), &parse_port, SAFESTR2(prefix));
	*port = parse_port;

	// Must match at least first part.
	return matches >= 1 && *port < USHRT_MAX && strlen(server) >= 5;
}
