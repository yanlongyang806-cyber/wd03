
#include "cmdparse.h"
#include "DoorTransitionCommon.h"
#include "GameClientLib.h"
#include "GCLBaseStates.h"
#include "GfxSprite.h"
#include "GlobalStateMachine.h"
#include "GraphicsLib.h"
#include "inputKeyBind.h"
#include "soundLib.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define PLAY_VIDEO_MAP_TRANSFER_TIMEOUT_MS 5000

static char* s_pchVideoPath = NULL;
static DoorTransitionType s_eTransitionType = kDoorTransitionType_Unspecified;
static U32 s_uMapTransferTimeout = 0;
static bool s_bFullscreen = false;

void gclPlayVideo_Stop(void)
{
	// Stop the video
	gfxFMVClose();

 	if (GSM_IsStateActive(GCL_GAMEPLAY))
	{
		if (s_eTransitionType != kDoorTransitionType_Unspecified)
		{
			if (s_eTransitionType == kDoorTransitionType_Departure)
			{
				s_uMapTransferTimeout = gGCLState.totalElapsedTimeMs + PLAY_VIDEO_MAP_TRANSFER_TIMEOUT_MS;
			}
			ServerCmd_PlayVideo_Finished();
		}
		if (s_eTransitionType != kDoorTransitionType_Departure)
		{
			GSM_SwitchToState_Complex(GCL_GAMEPLAY);
		}
	}
	else if (GSM_IsStateActive(GCL_LOGIN_NEW_CHARACTER_CREATION))
    {
        GSM_SwitchToState_Complex(GCL_LOGIN_NEW_CHARACTER_CREATION);
    }
    else
	{
		// Switch to the login state
		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN);
	}
}

void gclPlayVideo_Enter(void)
{
	if (s_bFullscreen)
	{
		// Hide the UI
		globCmdParse("-ShowGameUINoExtraKeyBinds");

		// Disable in-game sound
		globCmdParse("sndDisable");
	}
	// Push the FMV keybind profile
	keybind_PushProfileName("FMVKeyBinds");

	// Play the launch video
	gfxFMVPlayFullscreen(s_pchVideoPath);
}

void gclPlayVideo_BeginFrame(void)
{
	// See if the video is still playing
	if (gfxFMVDone())
	{
		if (!s_uMapTransferTimeout)
		{
			gclPlayVideo_Stop();
		}
		else if (gGCLState.totalElapsedTimeMs >= s_uMapTransferTimeout)
		{
			GSM_SwitchToState_Complex(GCL_GAMEPLAY);
			s_uMapTransferTimeout = 0;
		}
	}
	else
	{
		F32 fVolume = sndGetAdjustedVolumeByType(SND_VIDEO);

		// Update the volume
		gfxFMVSetVolume(fVolume);
	}
}

void gclPlayVideo_EndFrame(void)
{
	if (s_bFullscreen)
	{
		int iScreenWidth, iScreenHeight;
		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);

		display_sprite_tex(white_tex, 0, 0, -1, iScreenWidth / (F32)texWidth(white_tex),
			iScreenHeight / (F32)texHeight(white_tex), 0x000000FF);
	}
}

void gclPlayVideo_Leave(void)
{
	if (s_bFullscreen)
	{
		// Show the UI
		globCmdParse("+ShowGameUINoExtraKeyBinds");

		// Enable in-game sound
		globCmdParse("sndEnable");
	}
	// Pop the FMV keybind profile
	keybind_PopProfileName("FMVKeyBinds");

	if (s_pchVideoPath)
		SAFE_FREE(s_pchVideoPath);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(PlayVideo_Start) ACMD_PRIVATE;
void gclPlayVideo_Start(const char* pchVideoPath, DoorTransitionType eTransitionType, bool bFullscreen)
{
	if (gfxFMVDone())
	{
		if (GSM_IsStateActive(GCL_GAMEPLAY))
		{
			GSM_SwitchToState_Complex(GCL_GAMEPLAY "/" GCL_PLAY_VIDEO);
		}
		else
		{
			GSM_AddChildState(GCL_PLAY_VIDEO, false);
		}
		if (s_pchVideoPath)
			SAFE_FREE(s_pchVideoPath);
		s_pchVideoPath = strdup(pchVideoPath);
		s_eTransitionType = eTransitionType;
		s_bFullscreen = bFullscreen;
		s_uMapTransferTimeout = 0;
	}
}

// This allows the player to skip FMV
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME("SkipFMV");
void gclPlayVideo_SkipFMV(Entity *e)
{
	gclPlayVideo_Stop();
}