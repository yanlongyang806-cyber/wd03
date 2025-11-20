#include "voice.h"
#include "gclEntity.h"
#include "team.h"
#include "AutoTransDefs.h"

#if _XBOX

#include "xlivelib.h"
#include "XBoxStructs_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

HWND g_VoiceHWND = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("voice.cpp", BUDGET_GameSystems););


AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(VoiceChat_Join);
void voiceChatCmdJoin()
{
	ServerCmd_Team_JoinVoiceChat();
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(VoiceChat_Leave);
void voiceChatCmdLeave()
{
	ServerCmd_Team_LeaveVoiceChat();
}


// This command sets the debug output level for Xbox/XLive calls
// for PC, the level must be between 0 - 3
// for Xbox, it must be between 0 - 5
AUTO_COMMAND ACMD_CLIENTCMD;
void setXboxSystemOutputLevel(U32 level)
{
#if !defined(PROFILE)
	if (level < 0)
		level = 0;

	if (level > 5)
		level = 5;
	XDebugSetSystemOutputLevel(HXAMAPP_XLIVEBASE, level);
	XDebugSetSystemOutputLevel(HXAMAPP_XGI, level);
#endif // !defined PROFILE
}


#ifndef _XBOX

#include "dsound.h"
#include "GameClientLib.h"
#include "rdrDevice.h"
static LPDIRECTSOUND g_pDS = NULL;

int initDirectSound()
{
	loadstart_printf("Initializing Direct Sound...");
	if (gGCLState.pPrimaryDevice && gGCLState.pPrimaryDevice->device && (DS_OK==DirectSoundCreate(NULL,&g_pDS,NULL))) //create direct sound object
	{
		g_VoiceHWND = gGCLState.pPrimaryDevice?(HWND)gGCLState.pPrimaryDevice->device->getWindowHandle(gGCLState.pPrimaryDevice->device):NULL;
		//ok, DirectSound Object created, let's take control now...
		if (DS_OK != IDirectSound_SetCooperativeLevel(g_pDS, g_VoiceHWND, DSSCL_EXCLUSIVE))
		{
			loadend_printf("	Failed to set DirectSound Cooperative Level.\n");
			return 0;
		}
	}
	else
	{
		loadend_printf("	Failed to initailze DirectSound.\n");
		return 0;
	}

	loadend_printf("Done");

	return 1;
}

#endif


void voiceClearIsTalkingFlags(void)
{
	Entity *pEnt = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pEnt);

	if (pTeam)
	{
		int i;
		for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			TeamMember *pMember = pTeam->eaMembers[i];
			if (pMember) {
				//pMember->isVoiceChatting = 0.0f;
			}
		}
	}
}

void voiceSetTeamMemberIsTalking(ContainerID memberID)
{
	Entity *pEnt = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pEnt);

	if (pTeam)
	{
		int i;
		for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			TeamMember *pMember = pTeam->eaMembers[i];
			if (pMember && pMember->iEntID == memberID)
			{
				// this is how many seconds this user will show up as 'is talking'
				//pMember->isVoiceChatting = 1.0f;
				return;
			}
		}
	}
}

bool voiceIsTeamMemberTalking(ContainerID memberID)
{
	Entity *pEnt = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pEnt);

	if (pTeam)
	{
		int i;
		for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			TeamMember *pMember = pTeam->eaMembers[i];
			if (pMember && pMember->iEntID == memberID)
			{
				return false;//pMember->isVoiceChatting > 0.0f;
			}
		}
	}

	return false;
}

void voiceGetMyIdentInfo(ContainerID *cid, XUID *xuid)
{
	Entity *pEnt = entActivePlayerPtr();

	if (!cid || !xuid)
		return;

	if (pEnt)
	{
		*cid = pEnt->myContainerID;
		XUserGetXUID(0, xuid);
	}
}

#endif
