#if !PLATFORM_CONSOLE

#include "gclMediaControl.h"
#include "NotifyCommon.h"
#include "GfxConsole.h"

#include "earray.h"
#include "utils.h"
#include "file.h"
#include "fileutil2.h"
#include "event_sys.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static char g_last_song[MAX_PATH];
static FMOD_SOUND *active_sound = NULL;
static FMOD_CHANNEL *active_chan = NULL;
static float g_volume = 0.5;

#define FMOD_DO(fn) {result = fn; assert(result==FMOD_OK);}

static void playerNext(void);

static FMOD_RESULT F_CALLBACK channelCB(FMOD_CHANNEL *channel, FMOD_CHANNEL_CALLBACKTYPE type, void *commanddata1, void *commanddata2)
{
	if(type == FMOD_CHANNEL_CALLBACKTYPE_END)
	{
		playerNext();
	}
	return FMOD_OK;
}

static void playFile(const char *path, bool play)
{
	FMOD_RESULT result;
	FMOD_SYSTEM *system = fmodGetSystem();
	FMOD_TAG tag = {0};
	char *title=NULL, *artist=NULL, *album=NULL;
	unsigned int length;
	if(!system) return;
	if(active_chan)
	{
		FMOD_DO(FMOD_Channel_SetCallback(active_chan, NULL));
		FMOD_DO(FMOD_Channel_Stop(active_chan));
		active_chan = NULL;
	}
	if(active_sound)
	{
		FMOD_DO(FMOD_Sound_Release(active_sound));
		active_sound = NULL;
	}
	FMOD_DO(FMOD_System_CreateStream(system, path, FMOD_DEFAULT, NULL, &active_sound));
	FMOD_DO(FMOD_System_PlaySound(system, FMOD_CHANNEL_FREE, active_sound, true, &active_chan));
	FMOD_DO(FMOD_Channel_SetVolume(active_chan, g_volume));
	FMOD_DO(FMOD_Channel_SetCallback(active_chan, channelCB));
	result = FMOD_Sound_GetTag(active_sound, "TITLE", 0, &tag);
	if(result == FMOD_OK)
		title = strdup(tag.data);
	result = FMOD_Sound_GetTag(active_sound, "ARTIST", 0, &tag);
	if(result == FMOD_OK)
		artist = strdup(tag.data);
	result = FMOD_Sound_GetTag(active_sound, "ALBUM", 0, &tag);
	if(result == FMOD_OK)
		album = strdup(tag.data);
	result = FMOD_Sound_GetLength(active_sound, &length, FMOD_TIMEUNIT_MS);
	if(result != FMOD_OK)
		result = 0;
	gclMediaControlUpdate(play?1:0, title, album, artist, g_volume*100, 0, length/1000);
	SAFE_FREE(title);
	SAFE_FREE(artist);
	SAFE_FREE(album);	
	strcpy(g_last_song, path);
	if(play)
		FMOD_DO(FMOD_Channel_SetPaused(active_chan, false));
}

static int cmpFiles(const char **a, const char **b)
{
	return stricmp(*a, *b);
}

static void playerConnect(void)
{
	char **files, base[MAX_PATH];
	const char *vol = gclMediaControlGetPref("MP3", "Volume", NULL);
	const char *folder = gclMediaControlGetPref("MP3", "Folder", "mp3");
	if(vol)
		g_volume = atoi(vol) / 100.0;
	fileLocateWrite(folder, base);
	files = fileScanDirNoSubdirRecurse(base);
	if(eaSize(&files))
	{
		eaQSort(files, cmpFiles);
		playFile(files[0], false);
	}
	fileScanDirFreeNames(files);
}

static void playerDisconnect(void)
{
	FMOD_RESULT result;
	gclMediaControlSetPref("MP3", "Volume", "%u", (int)(g_volume*100));
	if(active_chan)
	{
		FMOD_DO(FMOD_Channel_SetCallback(active_chan, NULL));
		FMOD_DO(FMOD_Channel_Stop(active_chan));
		active_chan = NULL;
	}
	if(active_sound)
	{
		FMOD_DO(FMOD_Sound_Release(active_sound));
		active_sound = NULL;
	}
}

static void playerPlayPause(void)
{
	FMOD_RESULT result;
	if(active_chan)
	{
		FMOD_BOOL paused;
		unsigned int pos;
		FMOD_DO(FMOD_Channel_GetPaused(active_chan, &paused));
		FMOD_DO(FMOD_Channel_SetPaused(active_chan, !paused));
		FMOD_DO(FMOD_Channel_GetPosition(active_chan, &pos, FMOD_TIMEUNIT_MS));
		gclMediaControlUpdate(paused?1:0, NULL, NULL, NULL, -1, pos/1000.0, -1);
	}	
}

static void playerPrevious(void)
{
	FMOD_RESULT result;
	FMOD_BOOL paused = false;
	char **files, base[MAX_PATH];
	const char *folder = gclMediaControlGetPref("MP3", "Folder", "mp3");
	fileLocateWrite(folder, base);
	files = fileScanDirNoSubdirRecurse(base);
	if(eaSize(&files))
	{
		int i;
		char *key = g_last_song;
		eaQSort(files, cmpFiles);
		i = (int)eaBFind(files, cmpFiles, key);
		i -= 1;
		if(i >= eaSize(&files))
			i = 0;
		else if(i < 0)
			i = eaSize(&files) - 1;
		if(active_chan)
			FMOD_DO(FMOD_Channel_GetPaused(active_chan, &paused));
		playFile(files[i], !paused);
	}
	fileScanDirFreeNames(files);
}


static void playerNext(void)
{
	FMOD_RESULT result;
	FMOD_BOOL paused = false;
	char **files, base[MAX_PATH];
	const char *folder = gclMediaControlGetPref("MP3", "Folder", "mp3");
	fileLocateWrite(folder, base);
	files = fileScanDirNoSubdirRecurse(base);
	if(eaSize(&files))
	{
		int i;
		char *key = g_last_song;
		eaQSort(files, cmpFiles);
		i = (int)eaBFind(files, cmpFiles, key);
		if(stricmp(files[i], g_last_song)==0)
			i += 1;
		if(i >= eaSize(&files))
			i = 0;
		else if(i < 0)
			i = eaSize(&files) - 1;
		if(active_chan)
			FMOD_DO(FMOD_Channel_GetPaused(active_chan, &paused));
		playFile(files[i], !paused);
	}
	fileScanDirFreeNames(files);
}

static void playerVolume(U32 volume)
{
	FMOD_RESULT result;
	g_volume = volume / 100.0;
	if(active_chan)
	{
		FMOD_DO(FMOD_Channel_SetVolume(active_chan, g_volume));
	}
}

static void playerTime(float time)
{
	FMOD_RESULT result;
	if(active_chan)
	{
		FMOD_DO(FMOD_Channel_SetPosition(active_chan, time*1000, FMOD_TIMEUNIT_MS));
	}
}


AUTO_RUN;
void gclMediaControlMP3Register(void)
{
	if(isDevelopmentMode())
		gclMediaControlRegister("MP3", playerConnect, playerDisconnect, NULL, playerPlayPause, playerPrevious, playerNext, playerVolume, playerTime);
}

#endif