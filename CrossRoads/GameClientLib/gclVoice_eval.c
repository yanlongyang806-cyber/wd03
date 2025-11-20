#include "sndVoice.h"

#include "gclEntity.h"
#include "EntityLib.h"
#include "GlobalTypes.h"
#include "Guild.h"
#include "Player.h"
#include "UIGen.h"

#include "gclVoice_eval_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

AUTO_STRUCT;
typedef struct VoiceParticipant {
	int id;
} VoiceParticipant;

VoiceParticipant **g_Participants;

AUTO_COMMAND ACMD_NAME(VoicePlayAd) ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void svCmdVoicePlayAd(const char* channel)
{
	svPlayAd(channel);
}

AUTO_COMMAND ACMD_NAME(svFontsPrint);
void svCmdPrintFonts(void)
{
	svFontsPrint();
}

AUTO_COMMAND ACMD_NAME(svDumpRecord);
void svCmdDumpRecord(void)
{
	svChannelGroupDumpRecording(g_VoiceState.defGroup);
}

AUTO_COMMAND ACMD_NAME(VoiceRecordInit);
int svCmdRecordInit(void)
{
	return svRecordInit();
}

AUTO_COMMAND ACMD_NAME(VoiceRecordClose);
int svCmdRecordClose(void)
{
	return svRecordClose();
}

AUTO_COMMAND ACMD_NAME(VoiceRecordStart);
void svCmdRecordStart(void)
{
	svRecordStart();
}

AUTO_COMMAND ACMD_NAME(VoiceRecordStop);
void svCmdRecordStop(void)
{
	svRecordStop();
}

AUTO_COMMAND ACMD_NAME(VoiceRecordPlay);
void svCmdRecordPlay(int fontID)
{
	svRecordQueuePlay(fontID);
}

AUTO_COMMAND ACMD_NAME(EnableVoice) ACMD_CMDLINE;
void svCmdEnableVoice(U32 on)
{
	gConf.bVoiceChat = on;
}

AUTO_COMMAND ACMD_NAME(svFlagsClear);
void svCmdFlagsClear(void)
{
	FOR_EACH_IN_EARRAY(g_VoiceState.channels, VoiceChannel, chan)
	{
		chan->has_listened = 0;
		chan->has_spoken = 0;
	}
	FOR_EACH_END
}

AUTO_COMMAND ACMD_NAME(svChannelJoin) ACMD_ACCESSLEVEL(0);
void svCmdChannelJoin(const char* chan)
{
	svChannelJoinByName(NULL, chan, chan, true);
}

AUTO_COMMAND ACMD_NAME(svChannelJoinWithGroup) ACMD_ACCESSLEVEL(9);
void svCmdChannelJoinWithGroup(int groupId, const char* chan)
{
	VoiceChannelGroup* group = svChannelGroupFind(groupId, NULL);

	svChannelJoinByName(group, chan, chan, true);
}

AUTO_COMMAND ACMD_NAME(svChannelLeave) ACMD_ACCESSLEVEL(0);
void svCmdChannelLeave(const char* chan)
{
	svChannelLeaveByName(chan, chan, true);
}

AUTO_COMMAND ACMD_NAME(svMicSetLevel) ACMD_ACCESSLEVEL(0);
void svCmdMicSetLevel(int level)
{
	svMicrophoneSetLevel(level);
}

AUTO_COMMAND ACMD_NAME(svSpeakersSetLevel) ACMD_ACCESSLEVEL(0);
void svCmdSpeakersSetLevel(int level)
{
	svSpeakersSetLevel(level);
}

AUTO_COMMAND ACMD_NAME(svPushToTalk) ACMD_ACCESSLEVEL(0);
void svCmdSetTransmit(int on)
{
	svPushToTalk(on);
}

AUTO_COMMAND ACMD_NAME(svSetMute) ACMD_ACCESSLEVEL(0);
void svCmdSetMute(int on)
{
	svMicrophoneSetMute(on);
}

AUTO_COMMAND ACMD_NAME(svSetLogLevel);
void svCmdSetLogLevel(ACMD_NAMELIST(VivoxLogLevelsEnum, STATICDEFINE) char *levelStr)
{
	int level = StaticDefineIntGetInt(VivoxLogLevelsEnum, levelStr);

	if(level<0)
		return;

	svSetLogLevel(level);
}

// Returns whether voice system is allowed by the game config
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceAllowedByConf);
int exprFuncVoiceAllowedByConf(void)
{
	return gConf.bVoiceChat;
}

// Returns whether voice is enabled
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceEnabled);
int exprFuncVoiceEnabled(void)
{
	return !g_VoiceState.options.NoVoice;
}

// Sets up voice recording... continue calling till true is returned
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceRecordSetup);
int exprFuncVoiceRecordSetup(void)
{
	return svRecordInit();
}

// Tears down voice recording... continue calling till true is returned
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceRecordTearDown);
int exprFuncVoiceRecordTeardown(void)
{
	return svRecordClose();
}

// Starts voice recording for voice font testing or simple mic input testing
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceRecordStart);
void exprFuncVoiceRecordStart(void)
{
	svRecordStart();
}

// Stops voice recording
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceRecordStop);
void exprFuncVoiceRecordStop(void)
{
	svRecordStop();
}

// Plays the recorded voice (FontID not used yet)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceRecordPlaybackStart);
void exprFuncVoiceRecordPlaybackStart(int fontID)
{
	svRecordQueuePlay(fontID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceRecordPlaybackCompleted);
int exprFuncVoiceRecordPlaybackCompleted(void)
{
	if(!g_VoiceState.record_active)
		return 0;

	return g_VoiceState.record_playback_complete;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceRecordEnergy);
F32 exprFuncVoiceRecordGetEnergy(void)
{
	if(!g_VoiceState.record_active || !g_VoiceState.record_mic_active)
		return 0;

	return g_VoiceState.record_energy;
}

// Sets the mic into open mic (on=1) or ptt (on=0)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceMicSetOpen);
void exprFuncVoiceMicSetOpen(int on)
{
	svSetOpenMic(on);
}

// Gets open mic state
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceMicGetOpen);
int exprFuncVoiceMicGetOpen(void)
{
	return svMicGetOpen();
}

// Sets microphone input level (0-1)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceMicSetLevel);
void exprFuncVoiceMicSetLevel(F32 level)
{
	if(level < 0) return;
	if(level > 1) return;

	svMicrophoneSetLevel(level*100);
}

// Gets microphone input level (0-1)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceMicGetLevel);
F32 exprFuncMicGetLevel(void)
{
	F32 level = svMicrophoneGetLevel();

	return level/100;
}

//Gets list of VoiceFonts usable by self recording
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetFontList);
void exprFuncVoiceGetFontList(UIGen *gen, U32 templates)
{
	VoiceFont ***fonts = ui_GenGetManagedListSafe(gen, VoiceFont);
	eaClearFast(fonts);

	if(templates)
		eaCopy(fonts, &g_VoiceState.template_fonts);
	else
		eaCopy(fonts, &g_VoiceState.session_fonts);

	ui_GenSetManagedListSafe(gen, fonts, VoiceFont, false);
}

// Gets id from VoiceFont
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceFontGetID);
int exprFuncVoiceFontGetID(SA_PARAM_OP_VALID VoiceFont *font)
{
	if(!font)
		return 0;

	return font->id;
}

// Gets name from VoiceFont
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceFontGetName);
const char* exprFuncVoiceFontGetName(SA_PARAM_OP_VALID VoiceFont *font)
{
	if(!font)
		return "";

	return font->name;
}

// Gets desc from VoiceFont
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceFontGetDesc);
const char* exprFuncVoiceFontGetDesc(SA_PARAM_OP_VALID VoiceFont *font)
{
	if(!font)
		return "";

	return font->desc;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceSetFont);
void exprFuncVoiceSetFont(const char* chanName, int fontId)
{
	svChannelSetFontByName(chanName, NULL, fontId);
}

Entity* entSubscribedFromAccount(U32 accountID)
{
	RefDictIterator iter;
	Entity *e;
	
	RefSystem_InitRefDictIterator(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), &iter);
	while(e = (Entity*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(entGetAccountID(e)==accountID)
			return e;
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantList);
void exprFuncVoiceGetParticipantList(UIGen *gen, const char* chanName, U32 speakingOnly)
{
	VoiceParticipant ***vps = ui_GenGetManagedListSafe(gen, VoiceParticipant);
	static ContainerID *ids = NULL;
	int i;
	eaClearFast(vps);

	ea32ClearFast(&ids);
	svChannelGetParticipantListByName(chanName, NULL, &ids, speakingOnly);

	for(i=ea32Size(&ids)-1; i>=0; i--)
	{
		Entity *e = NULL;
		VoiceParticipant *vp = eaGet(&g_Participants, i);

		if(!vp)
		{
			vp = StructCreate(parse_VoiceParticipant);
			eaSet(&g_Participants, vp, i);
		}
		vp->id = ids[i];

		eaPush(vps, vp);
	}

	ui_GenSetManagedListSafe(gen, vps, VoiceParticipant, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantListAsString);
char* exprFuncVoiceGetParticipantListAsString(SA_PARAM_NN_VALID UIGen *gen, const char* chanName, U32 speakingOnly)
{
	static ContainerID *ids = NULL;
	int i;
	static char *estr = NULL;
	
	ea32ClearFast(&ids);
	svChannelGetParticipantListByName(chanName, NULL, &ids, speakingOnly);

	estrClear(&estr);
	for(i=ea32Size(&ids)-1; i>=0; i--)
	{
		Entity *e = NULL;

		e = entFromAccountID(ids[i]);

		if(e)
			estrConcatf(&estr, "%s%s", i==0 ? "" : ", ", entGetLocalName(e));
	}

	return estr;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantName);
char* exprFuncVoiceGetParticipantName(SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	Entity *e = NULL;
	Entity *local = entActivePlayerPtr();

	if(!vp)
		return "Unknown";

	e = entFromAccountID(vp->id);

	if(e)
		return (char*)entGetLocalName(e);

	e = entSubscribedFromAccount(vp->id);

	if(e)
		return (char*)entGetLocalName(e);

	if(local && local->pPlayer->pGuild)
	{
		Guild* g = GET_REF(local->pPlayer->pGuild->hGuild);

		if(g)
		{
			FOR_EACH_IN_EARRAY(g->eaMembers, GuildMember, member)
			{
				if(member->iAccountID == (U32)vp->id)
					return (char*)member->pcName;
			}
			FOR_EACH_END
		}
	}

	return "Unknown";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceParticipantFromEnt);
SA_RET_OP_VALID VoiceParticipant* exprFuncVoiceParticipantFromEnt(SA_PARAM_OP_VALID Entity *e)
{
	int id = 0;
	VoiceParticipant *vp;

	if(!e)
		return NULL;

	id = entGetAccountID(e);

	FOR_EACH_IN_EARRAY(g_Participants, VoiceParticipant, vpTest)
	{
		if(vpTest->id==id)
			return vpTest;
	}
	FOR_EACH_END

	vp = StructCreate(parse_VoiceParticipant);
	vp->id = id;
	eaPush(&g_Participants, vp);

	return vp;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceIsParticipant);
bool exprFuncVoiceIsParticipant(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	ContainerID id = vp ? vp->id : 0;

	if(id)
	{
		VoiceChannelUser *vcu = svChannelFindUserByName(chanName, NULL, id);

		return !!vcu;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantVolume);
F32 exprFuncVoiceGetParticipantVolume(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	ContainerID id = vp ? vp->id : 0;

	if(id)
	{
		VoiceChannelUser *vcu = svChannelFindUserByName(chanName, NULL, id);

		if(vcu)
			return vcu->user->volume / 100.0;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantTargetVolume);
F32 exprFuncVoiceGetParticipantTargetVolume(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	ContainerID id = vp ? vp->id : 0;

	if(id)
	{
		VoiceChannelUser *vcu = svChannelFindUserByName(chanName, NULL, id);

		if(vcu)
			return vcu->user->targetVolume / 100.0;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantEnergy);
F32 exprFuncVoiceGetParticipantEnergy(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	ContainerID id = vp ? vp->id : 0;

	if(id)
	{
		VoiceChannelUser *vcu = svChannelFindUserByName(chanName, NULL, id);

		if(vcu)
			return vcu->energy;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantMuted);
F32 exprFuncVoiceGetParticipantMuted(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	ContainerID id = vp ? vp->id : 0;

	if(id)
	{
		VoiceChannelUser *vcu = svChannelFindUserByName(chanName, NULL, id);

		if(vcu)
			return vcu->user->isMutedByMe || vcu->isMutedByOp;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantMutedByMe);
F32 exprFuncVoiceGetParticipantMutedByMe(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	ContainerID id = vp ? vp->id : 0;

	if(id)
	{
		VoiceChannelUser *vcu = svChannelFindUserByName(chanName, NULL, id);

		if(vcu)
			return vcu->user->isMutedByMe;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantMutedByOp);
F32 exprFuncVoiceGetParticipantMutedByOp(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	ContainerID id = vp ? vp->id : 0;

	if(id)
	{
		VoiceChannelUser *vcu = svChannelFindUserByName(chanName, NULL, id);

		if(vcu)
			return vcu->isMutedByOp;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetParticipantIsTalking);
F32 exprFuncVoiceGetParticipantIsTalking(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp)
{
	ContainerID id = vp ? vp->id : 0;

	if(id)
	{
		VoiceChannelUser *vcu = svChannelFindUserByName(chanName, NULL, id);

		if(vcu)
			return vcu->isTalking;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceSetUserChannelVolume);
void exprFuncVoiceSetUserChannelVolume(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp, F32 vol)
{
	ContainerID id = vp ? vp->id : 0;

	MINMAX1(vol, 0, 1);

	if(id)
		svUserSetVolumeByName(NULL, NULL, id, vol*100);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceSetUserChannelMute);
void exprFuncVoiceSetUserChannelMute(const char* chanName, SA_PARAM_OP_VALID VoiceParticipant *vp, bool mute)
{
	ContainerID id = vp ? vp->id : 0;

	if(g_VoiceState.localAcct==id)
		svUserSetSelfMute(id, mute);
	else if(id)
		svUserSetMuteByID(id, mute);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceGetActiveChannel);
const char* exprFuncVoiceGetActiveChannel(void)
{
	return svChannelGetName(NULL, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VoiceChannelGetFont);
SA_RET_OP_VALID VoiceFont* exprFuncVoiceChannelGetFont(const char* chanName)
{
	return svChannelGetFontByName(chanName, NULL);
}

#include "gclVoice_eval_c_ast.c"