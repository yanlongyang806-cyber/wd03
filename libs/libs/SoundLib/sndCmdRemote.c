#include "sndLibPrivate.h"
#include "mathutil.h"

#include "sndMusic.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndPlayRemote3d(const char *event_name, float x, float y, float z, const char *filename)
{
#ifndef STUB_SOUNDLIB
	sndPlayAtPosition(event_name, x, y, z, filename, -1, NULL, NULL, false);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndPlayRemote3dV2(const char *event_name, float x, float y, float z, const char *filename, U32 entRef)
{
#ifndef STUB_SOUNDLIB
	sndPlayAtPosition(event_name, x, y, z, filename, entRef, NULL, NULL, false);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndPlayRemote3dFromEntity(const char *event_name, U32 entRef, const char *filename)
{
#ifndef STUB_SOUNDLIB
	sndPlayFromEntity(event_name, entRef, filename, false);
#endif
}

// This is kept here so Dave's demos work
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndPlayRemote(const char *event_name, const char *filename)
{
#ifndef STUB_SOUNDLIB
	Vec3 player_pos;
	if(!sndEnabled())
		return;
	
	sndGetPlayerPosition(player_pos);

	sndPlayRemote3dV2(event_name, vecParamsXYZ(player_pos), filename, -1);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndPlayRemoteV2(const char *event_name, const char *filename, U32 entRef)
{
#ifndef STUB_SOUNDLIB
	Vec3 player_pos;
	if(!sndEnabled())
		return;
	
	sndGetPlayerPosition(player_pos);

	sndPlayRemote3dV2(event_name, vecParamsXYZ(player_pos), filename, entRef);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndStopOneShot(const char *event_name)
{
#ifndef STUB_SOUNDLIB
	sndStopRemote(event_name);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndReplaceMusic(char *eventname, char *filename, U32 entRef)
{
#ifndef STUB_SOUNDLIB
	sndMusicReplace(eventname, filename, entRef);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndClearMusic(void)
{
#ifndef STUB_SOUNDLIB
	sndMusicClear(false);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndEndMusic(void)
{
#ifndef STUB_SOUNDLIB
	sndMusicEnd(false);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndPlayMusic(char *event_name, char *filename, U32 entRef)
{
#ifndef STUB_SOUNDLIB
	sndMusicPlayRemote(event_name, filename, entRef);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void sndCritterDeath(U32 entRef)
{
#ifndef STUB_SOUNDLIB
	sndCritterClean(entRef);
#endif
}


