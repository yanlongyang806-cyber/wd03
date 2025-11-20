#if !PLATFORM_CONSOLE

#include "gclMediaControl.h"
#include "NotifyCommon.h"
#include "GfxConsole.h"
#include "Prefs.h"
#include "cmdparse.h"
#include "earray.h"
#include "soundLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static bool g_playing = false;
static char g_current_track[1024] = {0};
static char g_current_album[1024] = {0};
static char g_current_artist[1024] = {0};
static U32 g_current_volume = 50;
static U32 g_current_time_total = 0;
static U32 g_current_time_current = 0;
static U32 g_current_time_timer = 0;

static void checkPlayerInit(void);

typedef struct gclMediaControlImplementation
{
	char player[32];
	thunkCB connect;
	thunkCB disconnect;
	thunkCB tick;
	thunkCB playpause;
	thunkCB previous;
	thunkCB next;
	setVolumeCB volume;
	setTimeCB time;
} gclMediaControlImplementation;
static gclMediaControlImplementation **g_implementations = NULL;
static gclMediaControlImplementation *g_active_imp = NULL;
static char **g_player_names = NULL;

AUTO_RUN;
void gclMediaControlAutoRun(void)
{
	eaPush(&g_player_names, "None");
}

void gclMediaControlRegister(const char *player, thunkCB connect, thunkCB disconnect, thunkCB tick, thunkCB playpause, thunkCB previous, thunkCB next, setVolumeCB volume, setTimeCB time)
{
	gclMediaControlImplementation *imp = calloc(1, sizeof(gclMediaControlImplementation));
	strcpy(imp->player, player);
	imp->connect = connect;
	imp->disconnect = disconnect;
	imp->tick = tick;
	imp->playpause = playpause;
	imp->previous = previous;
	imp->next = next;
	imp->volume = volume;
	imp->time = time;
	eaPush(&g_implementations, imp);
	eaPush(&g_player_names, imp->player);
}

void gclMediaControlUpdate(int playing, const char *track, const char *album, const char *artist, int volume, float time_current, int time_total)
{
	if(playing != -1)
	{
		if(g_playing && !playing)
		{
			sndFadeInType(SND_MUSIC);
			sndFadeInType(SND_AMBIENT);
		}
		else if(!g_playing && playing)
		{
			sndFadeOutType(SND_MUSIC);	
			sndFadeOutType(SND_AMBIENT);
		}
		g_playing = playing;
	}
	if(track)
		strcpy_trunc(g_current_track, track);
	if(album)
		strcpy_trunc(g_current_album, album);
	if(artist)
		strcpy_trunc(g_current_artist, artist);
	if(volume != -1)
		g_current_volume = volume;
	if(time_current != -1)
	{
		if(!g_current_time_timer)
			g_current_time_timer = timerAlloc();
		timerStart(g_current_time_timer);
		g_current_time_current = time_current;
	}
	if(time_total != -1)
		g_current_time_total = time_total;
}

void gclMediaControlUpdateDisconnected(void)
{
	g_active_imp = NULL;
}

void gclMediaControlSetPref(const char *player, const char *key, const char *fmt, ...)
{
	char buf[128], val[1024];
	va_list ap;
	sprintf(buf, "MediaControl.%s.%s", player, key);
	va_start(ap, fmt);
	vsprintf(val, fmt, ap);
	va_end(ap);
	GamePrefStoreString(buf, val);
}

const char *gclMediaControlGetPref(const char *player, const char *key, const char *def)
{
	char buf[128];
	sprintf(buf, "MediaControl.%s.%s", player, key);
	return GamePrefGetString(buf, def);
}

CmdList gclMediaControlCmdList;

AUTO_COMMAND ACMD_NAME(mc, MediaControlCmd) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclMediaControlCmd(ACMD_NAMELIST(gclMediaControlCmdList, COMMANDLIST) ACMD_SENTENCE cmd)
{
	CmdContext context = {0};
	char* msg = NULL;

	checkPlayerInit();
	InitCmdOutput(context,msg);
	context.access_level = ACCESS_DEBUG;

	if(cmdCheckSyntax(&gclMediaControlCmdList, cmd, &context))
	{
		cmdParseAndExecute(&gclMediaControlCmdList, cmd, &context);
	}
	else
	{
		if(stricmp(cmd, "player")==0)
		{
			int i;
			for(i=0; i<eaSize(&g_implementations); i++)
			{
				conPrintf("%s\n", g_implementations[i]->player);
			}
		}
		else if(msg[0])
			conPrintf("%s", msg);
		else
			conPrintf("Incorrect mc command");
	}

	CleanupCmdOutput(context);
}

AUTO_COMMAND ACMD_NAME(player) ACMD_LIST(gclMediaControlCmdList) ACMD_ACCESSLEVEL(0);
void gclMediaControlCmdPlayer(const char *player)
{
	int i;
	gclMediaControlImplementation *new_imp = NULL;
	for(i=0; i<eaSize(&g_implementations); i++)
	{
		if(stricmp(g_implementations[i]->player, player)==0)
		{
			new_imp = g_implementations[i];
			break;
		}
	}
	// Allow both "none" and "" to disable
	if(!new_imp && stricmp(player, "none")!=0 && player[0])
	{
		conPrintf("Can't find player %s\n", player);
		return;
	}
	if(new_imp == g_active_imp)
		return;
	if(g_active_imp && g_active_imp->disconnect)
		g_active_imp->disconnect();
	gclMediaControlUpdate(0, "", "", "", 0, 0, 0);
	g_active_imp = new_imp;
	if(g_active_imp && g_active_imp->connect)
		g_active_imp->connect();
	GamePrefStoreString("MediaControl.Player", g_active_imp?g_active_imp->player:"");
}

static void checkPlayerInit(void)
{
	static bool inited = false;
	const char *player;

	if(inited)
		return;

	inited = true;
	player = GamePrefGetString("MediaControl.Player", "");
	if(player && player[0])
		gclMediaControlCmdPlayer(player);
}

AUTO_COMMAND ACMD_NAME(playpause) ACMD_LIST(gclMediaControlCmdList) ACMD_ACCESSLEVEL(0);
void gclMediaControlCmdPlayPause(void)
{
	if(g_active_imp && g_active_imp->playpause)
		g_active_imp->playpause();
}

AUTO_COMMAND ACMD_NAME(previous) ACMD_LIST(gclMediaControlCmdList) ACMD_ACCESSLEVEL(0);
void gclMediaControlCmdPrevious(void)
{
	if(g_active_imp && g_active_imp->previous)
		g_active_imp->previous();
}

AUTO_COMMAND ACMD_NAME(next) ACMD_LIST(gclMediaControlCmdList) ACMD_ACCESSLEVEL(0);
void gclMediaControlCmdNext(void)
{
	if(g_active_imp && g_active_imp->next)
		g_active_imp->next();
}

AUTO_COMMAND ACMD_NAME(volume) ACMD_LIST(gclMediaControlCmdList) ACMD_ACCESSLEVEL(0);
void gclMediaControlCmdVolume(U32 volume)
{
	g_current_volume = volume;
	if(g_active_imp && g_active_imp->volume)
		g_active_imp->volume(volume);
}

AUTO_COMMAND ACMD_NAME(show) ACMD_LIST(gclMediaControlCmdList) ACMD_ACCESSLEVEL(0);
void gclMediaControlCmdShow(void)
{
	globCmdParse("MediaControl_Toggle");
}


#endif

// If the user has a stored preference, try to connect to that client.
AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
void gclMediaControlTryConnect(void)
{
#if !PLATFORM_CONSOLE
	const char *player;
	if(g_active_imp)
		return;
	player = GamePrefGetString("MediaControl.Player", "");
	if(player && player[0])
		gclMediaControlCmdPlayer(player);
#endif
}

// Return if the connected media player is currently playing a song
AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
bool gclMediaControlIsPlaying(void)
{
#if !PLATFORM_CONSOLE
	return g_playing;
#else
	return false;
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
char *gclMediaControlCurrentTrack(void)
{
#if !PLATFORM_CONSOLE
	return g_current_track;
#else
	return "";
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
char *gclMediaControlCurrentAlbum(void)
{
#if !PLATFORM_CONSOLE
	return g_current_album;
#else
	return "";
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
char *gclMediaControlCurrentArtist(void)
{
#if !PLATFORM_CONSOLE
	return g_current_artist;
#else
	return "";
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
U32 gclMediaControlGetVolume(void)
{
#if !PLATFORM_CONSOLE
	return g_current_volume;
#else
	return 0;
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
void gclMediaControlSetVolume(float volume)
{
#if !PLATFORM_CONSOLE
	g_current_volume = volume;
	if(g_active_imp && g_active_imp->volume)
		g_active_imp->volume(volume);
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
U32 gclMediaControlGetTrackTotal(void)
{
#if !PLATFORM_CONSOLE
	return g_current_time_total;
#else
	return 0;
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
float gclMediaControlGetTrackCurrent(void)
{
#if !PLATFORM_CONSOLE
	float cur = g_current_time_current;
	if(g_playing)
		cur += timerElapsed(g_current_time_timer);
	return MIN(cur, g_current_time_total);
#else
	return 0;
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
void gclMediaControlSetTrackCurrent(float time)
{
#if !PLATFORM_CONSOLE
	if(!g_current_time_timer)
		g_current_time_timer = timerAlloc();
	timerStart(g_current_time_timer);
	g_current_time_current = time;
	if(g_active_imp && g_active_imp->time)
		g_active_imp->time(time);
#endif
}

// Run any needed per-frame processing
void gclMediaControlTick(void)
{
#if !PLATFORM_CONSOLE
	if(g_active_imp && g_active_imp->tick)
		g_active_imp->tick();
#endif
}

// Try to shutdown the connection to the media player
void gclMediaControlShutdown(void)
{
#if !PLATFORM_CONSOLE
	if(g_active_imp)
	{
		if(g_active_imp->disconnect)
			g_active_imp->disconnect();
		g_active_imp = NULL;
	}
#endif
}

// Set the active player
void gclMediaControlSetPlayer(const char *player)
{
#if !PLATFORM_CONSOLE
	gclMediaControlCmdPlayer(player);
#endif
}


// Check what players are currently available
char **gclMediaControlPlayers(U32 *current)
{
#if !PLATFORM_CONSOLE
	if(current)
	{
		const char *player = GamePrefGetString("MediaControl.Player", NULL);
		if(player && player[0])
		{
			*current = eaFindString(&g_player_names, player);
			if(*current==-1)
				*current = 0;  // Hard-coded "none" player
		}
		else
			*current = 0;
	}
	return g_player_names;
#else
	return NULL;
#endif
}
