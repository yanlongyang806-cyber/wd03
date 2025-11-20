#include "launcherUtils.h"
#include "registry.h"
#include "GameDetails.h"
#include "resource_CrypticLauncher.h"

#include "utils.h"
#include "file.h"
#include "error.h"
#include "EString.h"
#include "AppLocale.h"
#include "StashTable.h"
#include "ThreadSafeQueue.h"
#include "Prefs.h"
#include "sysutil.h"
#include "..\..\core\NewControllerTracker\pub\NewControllerTracker_pub.h"

static StashTable g_messages = NULL;
static char *g_messages_data_en = NULL;
static size_t g_messages_data_en_size = 0;
static char *g_messages_data = NULL;
static size_t g_messages_data_size = 0;

// Load the EN strings at startup
AUTO_RUN;
void loadMessageData(void)
{
	HRSRC rsrc = FindResourceEx(GetModuleHandle(NULL), "Messages", MAKEINTRESOURCE(IDR_MESSAGES), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT));
	if (rsrc)
	{
		HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
		if (gptr)
		{
			g_messages_data_en = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
			g_messages_data_en_size = SizeofResource(GetModuleHandle(NULL), rsrc);
			return;
		}
	}
	assertmsg(0, "Unable to load English message data");
}

void setMessageTable(int locid)
{
	HRSRC rsrc;
	char *str_en, *str;

	// Early out if already in current locale
	if(locid == getCurrentLocale())
		return;

	stashTableDestroy(g_messages);
	g_messages = NULL;
	
	if(locid == 0)
		return;

	rsrc = FindResourceEx(GetModuleHandle(NULL), "Messages", MAKEINTRESOURCE(IDR_MESSAGES), MAKELANGID(MAKELANGID(PRIMARYLANGID(locGetWindowsLocale(locid)), SUBLANG_DEFAULT), SUBLANG_DEFAULT));
	if (rsrc)
	{
		HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
		if (gptr)
		{
			g_messages_data = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
			g_messages_data_size = SizeofResource(GetModuleHandle(NULL), rsrc);
		}
	}
	assertmsgf(g_messages_data_size, "Unable to load message data for locale %d", locid);
	
	str_en = g_messages_data_en;
	str = g_messages_data;

	g_messages = stashTableCreateWithStringKeys(64, StashDefault);

	while(str_en < g_messages_data_en + g_messages_data_en_size)
	{
		assertmsg(str < g_messages_data + g_messages_data_size, "Localized message data exhausted before English?");

		stashAddPointer(g_messages, str_en, str, false);

		str_en += strlen(str_en) + 1;
		str += strlen(str) + 1;
	}
}

char *shardRootFolder(ShardInfo_Basic *shard)
{
	char *root=NULL, install_folder[MAX_PATH];
	const char *product;

	// Find the product display name
	product = gdGetDisplayName(gdGetIDByName(shard->pProductName));

	// Figure out where the root folder is
	// First look at the registry key
	if(!readRegStr(shard->pProductName, "InstallLocation", install_folder, MAX_PATH, NULL))
	{
		assert(_getcwd(install_folder, ARRAY_SIZE_CHECKED(install_folder)));
		estrPrintf(&root, "%s/%s/%s", install_folder, product, shard->pShardCategoryName);
		if(!dirExists(root))
		{
			if(!getExecutableDir(install_folder))
			{
				assert(_getcwd(install_folder, ARRAY_SIZE_CHECKED(install_folder)));
			}
		}
	}
	forwardSlashes(install_folder);

	estrPrintf(&root, "%s/%s/%s", install_folder, product, shard->pShardCategoryName);
	mkdirtree_const(root);
	return root;
}

int shardPrefSet(ShardInfo_Basic *shard)
{
	return PrefSetGet(STACK_SPRINTF("%s/localdata/gameprefs.pref", shardRootFolder(shard)));
}


const char *cgettext(const char *str)
{
	char *trans;

	if(!g_messages)
		return str;

	if(stashFindPointer(g_messages, str, &trans))
		return trans;
	else
		return str;
}

void setLauncherLocale(int locid)
{
	setMessageTable(locid);
	setCurrentLocale(locid);
}

void UTF8ToACP(const char *str, char *out, int len)
{
	wchar_t *wstr;
	int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	wstr = malloc(size * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, size);
	WideCharToMultiByte(CP_ACP, 0, wstr, -1, out, len, NULL, NULL);
	free(wstr);
}
 
void ACPToUTF8(const char *str, char *out, int len)
{
	wchar_t *wstr;
	int size = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
	wstr = malloc(size * sizeof(wchar_t));
	MultiByteToWideChar(CP_ACP, 0, str, -1, wstr, size);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out, len, NULL, NULL);
	free(wstr);
}

void postCommandString(XLOCKFREE_HANDLE queue, enumCrypticLauncherCommandType type, const char *str_value)
{
	CrypticLauncherCommand *cmd = malloc(sizeof(CrypticLauncherCommand));
	cmd->type = type;
	cmd->str_value = strdup(str_value);
	XLFQueueAdd(queue, cmd);
}

void postCommandInt(XLOCKFREE_HANDLE queue, enumCrypticLauncherCommandType type, U32 int_value)
{
	CrypticLauncherCommand *cmd = malloc(sizeof(CrypticLauncherCommand));
	cmd->type = type;
	cmd->int_value = int_value;
	XLFQueueAdd(queue, cmd);
}

void postCommandPtr(XLOCKFREE_HANDLE queue, enumCrypticLauncherCommandType type, void *ptr_value)
{
	CrypticLauncherCommand *cmd = malloc(sizeof(CrypticLauncherCommand));
	cmd->type = type;
	cmd->ptr_value = ptr_value;
	XLFQueueAdd(queue, cmd);
}

// Thread name setting code from FMOD
#define MS_VC_EXCEPTION 0x406D1388

typedef struct tagTHREADNAME_INFO
{
	DWORD dwType;       // Must be 0x1000.
	LPCSTR szName;      // Pointer to name (in user addr space).
	DWORD dwThreadID;   // Thread ID (-1=caller thread).
	DWORD dwFlags;      // Reserved for future use, must be zero.
} THREADNAME_INFO;

void SetThreadName(DWORD dwThreadID, LPCSTR szThreadName)
{
	THREADNAME_INFO info;

	info.dwType     = 0x1000;
	info.szName     = szThreadName;
	info.dwThreadID = dwThreadID;
	info.dwFlags    = 0;
	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR*)&info);
	}
#pragma warning(suppress : 6312)
	__except(EXCEPTION_CONTINUE_EXECUTION)
#pragma warning(suppress : 6322)
	{
	}
}
