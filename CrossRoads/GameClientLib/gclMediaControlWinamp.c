#if !PLATFORM_CONSOLE

#include "gclMediaControl.h"
#include "NotifyCommon.h"
#include "GfxConsole.h"
#include "StringUtil.h"
#include "utils.h"

#include "wininclude.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static bool g_stopped = false;

enum
{
	WINAMP_CMD_PREVIOUS = 40044,
	WINAMP_CMD_PLAY = 40045,
	WINAMP_CMD_PAUSE = 40046,
	WINAMP_CMD_NEXT = 40048,
};

enum
{
	WINAMP_USER_TRACKTIME = 105,
	WINAMP_USER_SETTIME = 106,
	WINAMP_USER_SETVOLUME = 122,
};

static void winampUpdateData(HWND hwnd)
{
	char buf[1024];
	char *tmp, *track, *artist, *status;
	bool playing = true;
	U32 current, total, volume;
	g_stopped = false;
	GetWindowText(hwnd, buf, ARRAY_SIZE(buf));
	if(strStartsWith(buf, "Winamp 5"))
	{
		g_stopped = true;
		gclMediaControlUpdate(0, "", "", "", 0, 0, 0);
		return;
	}
	artist = strchr(buf, '.');
	if(!artist) return;
	do 
	{
		artist += 1;
	} while (*artist == ' ');
	status = strrchr(buf, '[');
	if(status)
	{
		if(stricmp(status, "[paused]")==0)
			playing = false;
		else if(stricmp(status, "[stopped]")==0)
		{
			playing = false;
			g_stopped = true;
		}
	}
	tmp = strrstr(buf, " - Winamp");
	if(!tmp) return;
	*tmp = '\0';
	track = strrstr(artist, " - ");
	if(!track)
	{
		track = artist;
		artist = "";
	}
	else
	{
		*track = '\0';
		track += 3;
	}
	current = SendMessage(hwnd, WM_USER, 0, WINAMP_USER_TRACKTIME);
	total = SendMessage(hwnd, WM_USER, 1, WINAMP_USER_TRACKTIME);
	volume = SendMessage(hwnd, WM_USER, -666, WINAMP_USER_SETVOLUME);
	gclMediaControlUpdate(playing, track, NULL, artist, (volume * 100) / 255, current/1000.0, total);
}

static void winampTick(void)
{
	HWND hwnd = FindWindow("Winamp v1.x", NULL);
	if(hwnd)
		winampUpdateData(hwnd);
	else
		gclMediaControlUpdate(0, "", "", "", 0, 0, 0);
}

static void winampPlayPause(void)
{
	HWND hwnd = FindWindow("Winamp v1.x", NULL);
	if(hwnd)	
	{
		SendMessage(hwnd, WM_COMMAND, g_stopped?WINAMP_CMD_PLAY:WINAMP_CMD_PAUSE, (LPARAM)NULL);
		winampUpdateData(hwnd);
	}
}

static void winampPrevious(void)
{
	HWND hwnd = FindWindow("Winamp v1.x", NULL);
	if(hwnd)
	{
		SendMessage(hwnd, WM_COMMAND, WINAMP_CMD_PREVIOUS, (LPARAM)NULL);
		winampUpdateData(hwnd);
	}
}

static void winampNext(void)
{
	HWND hwnd = FindWindow("Winamp v1.x", NULL);
	if(hwnd)
	{
		SendMessage(hwnd, WM_COMMAND, WINAMP_CMD_NEXT, (LPARAM)NULL);
		winampUpdateData(hwnd);
	}
}

static void winampVolume(U32 volume)
{
	HWND hwnd = FindWindow("Winamp v1.x", NULL);
	if(hwnd)
	{
		U32 data = (volume * 255) / 100;
		SendMessage(hwnd, WM_USER, data, WINAMP_USER_SETVOLUME);
	}
}

static void winampTime(float time)
{
	HWND hwnd = FindWindow("Winamp v1.x", NULL);
	if(hwnd)
		SendMessage(hwnd, WM_USER, time * 1000, WINAMP_USER_SETTIME);
}

AUTO_RUN;
void gclMediaControlWinampRegister(void)
{
	gclMediaControlRegister("Winamp", NULL, NULL, winampTick, winampPlayPause, winampPrevious, winampNext, winampVolume, winampTime);
}

#endif