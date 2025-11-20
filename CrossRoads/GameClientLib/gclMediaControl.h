#pragma once
GCC_SYSTEM

typedef void (*thunkCB)(void);
typedef void (*setVolumeCB)(U32 volume);
typedef void (*setTimeCB)(float volume);

// Functions for plugins
void gclMediaControlRegister(const char *player, thunkCB connect, thunkCB disconnect, thunkCB tick, thunkCB playpause, thunkCB previous, thunkCB next, setVolumeCB volume, setTimeCB time);
void gclMediaControlUpdate(int playing, const char *track, const char *album, const char *artist, int volume, float time_current, int time_total);
void gclMediaControlUpdateDisconnected(void);
void gclMediaControlSetPref(const char *player, const char *key, const char *valuef, ...);
const char *gclMediaControlGetPref(const char *player, const char *key, const char *def);

// Functions for GameClient main loop
void gclMediaControlTick(void);
void gclMediaControlShutdown(void);

void gclMediaControlSetPlayer(const char *player);
char **gclMediaControlPlayers(U32 *current);
