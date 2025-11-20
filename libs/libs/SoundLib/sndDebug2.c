/***************************************************************************



***************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "FolderCache.h"
#include "stdtypes.h"

#include "sndLibPrivate.h"
#include "event_sys.h"

#include "sndConn.h"
#include "sndSpace.h"
#include "sndSource.h"

// From UI2Lib, for sound info printing
#include "UIProgressBar.h"
#include "UIScrollbar.h"
#include "UIWidgetTree.h"
#include "UIWindow.h"
#include "UISlider.h"
#include "UITextArea.h"
#include "CBox.h"
#include "UIButton.h"
#include "UICheckButton.h"
#include "UILabel.h"
#include "UISkin.h"
#include "UIList.h"
#include "UIBarGraph.h"
#include "GfxPrimitive.h"

// Util... for globCmdParse
#include "cmdparse.h"

#include "Prefs.h"

// For prefs obviously

#include "sndDebug2.h"

//#ifndef STUB_SOUNDLIB
#include "sndDebug2_c_ast.h"
//#endif

#include "gDebug.h"

#include "sndMusic.h"

AUTO_ENUM;
typedef enum SoundSourceDebugList {
	ssdlPos,
	ssdlDir,
	ssdlDist,
	ssdlVel,
	ssdlVol,
	ssdlBaseVol,
	ssdlPan,
	ssdlMode,
	ssdlPlaybacks,
	ssdlOneshot,
	ssdlFadeIn,
	ssdlFadeOut,
	ssdlDoppler,
	ssdlMax
} SoundSourceDebugList;

typedef void (*SoundSourceDebugVarFunc)(char **estrOut);

typedef struct DebugUserPane {
	SoundSource *testsource;
	UIButton *playtest;
	UISlider *volume_slider;
	UIButton *reset;
	REF_TO(DebuggerObject) debug_object;
	UIBarGraph *dspgraph_base;
	UIBarGraph *dspgraph_dsp;
	UIBarGraph *dspgraph_first;
	UILabel *memory_usage;
	UICheckButton *bypass_dsp;
} DebugUserPane;

typedef struct SoundMemoryDebugUI {
	UIScrollArea *memory_area;
	UIWindow *memory_window;
	UIWidgetTree *memory_tree;
} SoundMemoryDebugUI;

int gFlowNodeTick = 1;
int gMaxFlowNodeTick = 0;
CRITICAL_SECTION flow_node_crit;
StashTable snd_userpane_stash;
StashTable snd_flownode_stash;
SoundMemoryDebugUI smdu;
//StashTable snd_debug_leaves;
//SoundEventTreeNode **treeNodes = NULL;

typedef U32 (*eaCmpFunc)(const void *item1, const void *item2);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););




#ifdef STUB_SOUNDLIB

void sndDebuggerRebuildEventTree(void) { }
void sndDebugAddDSPEvent(int argb) { }
void sndDebuggerRegister(void) { }
void sndDebuggerCleanRefsToDSP(void *dsp) { }
void sndDebuggerHandleReload(void) { }
void sndUpdateDebugging(void) { }
void sndDebugSetCallbacks(void) { }

#else

void sndDebuggerSourceGetPos(char **estrOut);
void sndDebuggerSourceGetDir(char **estrOut);
void sndDebuggerSourceGetDist(char **estrOut);
void sndDebuggerSourceGetVel(char **estrOut);
void sndDebuggerSourceGetVol(char **estrOut);
void sndDebuggerSourceGetBaseVol(char **estrOut);
void sndDebuggerSourceGetPan(char **estrOut);
void sndDebuggerSourceGetMode(char **estrOut);
void sndDebuggerSourceGetPlaybacks(char **estrOut);
void sndDebuggerSourceGetOneshot(char **estrOut);
void sndDebuggerSourceGetFadeIn(char **estrOut);
void sndDebuggerSourceGetFadeOut(char **estrOut);

void sndToggleDSPSpaceBypass(UIAnyWidget *widget, UserData unused);
void sndToggleDSPConnectorBypass(UIAnyWidget *widget, UserData unused);
void sndDebuggerSourceGetDoppler(char **estrOut);

SoundSourceDebugVarFunc sndSourceDebugFuncs[] = {
	sndDebuggerSourceGetPos,
	sndDebuggerSourceGetDir,
	sndDebuggerSourceGetDist,
	sndDebuggerSourceGetVel,
	sndDebuggerSourceGetVol,
	sndDebuggerSourceGetBaseVol,
	sndDebuggerSourceGetPan,
	sndDebuggerSourceGetMode,
	sndDebuggerSourceGetPlaybacks,
	sndDebuggerSourceGetOneshot,
	sndDebuggerSourceGetFadeIn,
	sndDebuggerSourceGetFadeOut,
	sndDebuggerSourceGetDoppler
};

void sndProjectDebugMsgHandler(DebuggerObjectMsg *msg);
void sndGroupDebugMsgHandler(DebuggerObjectMsg *msg);
void sndEventDebugMsgHandler(DebuggerObjectMsg *msg);
void sndEventInstanceDebugMsgHandler(DebuggerObjectMsg *msg);
U32 sndBuildEventTree(char *name, void *e_g_c, SoundTreeType type, void *p_userdata, void *userdata, void **new_p_userdata);


void eaNotIntersect(cEArrayHandle *array1, cEArrayHandle *array2, eaCmpFunc func, cEArrayHandle *array1Out, cEArrayHandle *array2Out)
{
	int i;
	if(array1Out)
	{
		for(i=0; i<eaSize(array1); i++)
		{
			int j;
			int found = 0;
			for(j=0; !found && j<eaSize(array2); j++)
			{
				found = func((*array1)[i], (*array2)[j]);
			}
			if(!found)
			{
				eaPush(array1Out, array1[i]);
			}
		}
	}

	if(array2Out)
	{
		for(i=0; i<eaSize(array2); i++)
		{
			int j;
			int found = 0;
			for(j=0; !found && j<eaSize(array1); j++)
			{
				found = func((*array1)[j], (*array2)[i]);
			}
			if(!found)
			{
				eaPush(array2Out, array2[i]);
			}
		}
	}
	
}

static void sndDrawSpace(SoundSpace *space, F32 line_width)
{
	U32 opacity = 0;
	U32 col = 0;
	F32 max_aud = 0;
	F32 room = eaSize(&space->localSources) > 0 ? 0 : 1;
	Vec3 sub = {0.0001, 0.0001, 0.0001};

	if(eaSize(&space->localSources))
	{
		opacity = (U32)(0xFF << (24 - 1)) & 0xFF000000;
	}
	else
	{
		opacity = (U32)(0xFF << (24 - 3)) & 0xFF000000;
	}

	col = ((U32)(0xFF00FF * max_aud) & 0xFF00FF) + ((U32)(0x00FF00 * (1-max_aud)) & 0x00FF00);

	switch(space->type)
	{
		xcase SST_VOLUME: {
			// Hopefully get rid of fighting
			//gfxDrawBox3DARGB(min, max, space->volume.mat, opacity | col, line_width);
		}
		xcase SST_SPHERE:
			//gfxDrawSphere3DARGB(space->sphere.mid, space->sphere.radius, 10, opacity | col, line_width);
		break;
	}
}

static void sndDrawConn(SoundSpaceConnector *conn, F32 line_width)
{
	int j;
	Color color = ARGBToColor(0x7F00FFFF);

	if(conn->audibility>0)
	{
		color = ARGBToColor(0xAFAA00FF);
	}
	else
	{
		color = ARGBToColor(0xAF00AAFF);
	}

	gfxDrawBox3D(conn->local_min, conn->local_max, conn->world_mat, color, line_width);

	for(j=0; j<eaSize(&conn->props1.audibleConns); j++)
	{
		int k;
		int bidir = 0;
		SoundSpaceConnectorTransmission *trans = conn->props1.audibleConns[j];
		SoundSpaceConnectorProperties *otherProps = NULL;

		otherProps = sndConnGetSpaceProperties(trans->conn, conn->space1);

		for(k=0; k<eaSize(&otherProps->audibleConns); k++)
		{
			SoundSpaceConnectorTransmission *otherTrans = otherProps->audibleConns[k];

			if(otherTrans->conn==conn)
			{
				bidir = 1;
				break;
			}
		}

		if(bidir)
		{
			gfxDrawLine3DARGB(conn->world_mid, trans->conn->world_mid, 0xFFFF0000);
		}
		else
		{
			gfxDrawLine3D_2ARGB(conn->world_mid, trans->conn->world_mid, 0xFF00FF00, 0xFF0000FF);
		}
	}

	for(j=0; j<eaSize(&conn->props2.audibleConns); j++)
	{
		int k;
		int bidir = 0;
		SoundSpaceConnectorTransmission *trans = conn->props2.audibleConns[j];
		SoundSpaceConnectorProperties *otherProps = NULL;

		otherProps = sndConnGetSpaceProperties(trans->conn, conn->space2);

		if(otherProps)
		{
			for(k=0; k<eaSize(&otherProps->audibleConns); k++)
			{
				SoundSpaceConnectorTransmission *otherTrans = otherProps->audibleConns[k];

				if(otherTrans->conn==conn)
				{
					bidir = 1;
					break;
				}
			}
		}			

		if(bidir)
		{
			gfxDrawLine3DARGB(conn->world_mid, trans->conn->world_mid, 0xFFFF0000);
		}
		else
		{
			gfxDrawLine3D_2ARGB(conn->world_mid, trans->conn->world_mid, 0xFF00FF00, 0xFF0000FF);
		}
	}
}

void sndDebugAddDSPEvent(int argb)
{
	if(g_audio_dbg.setting_pane.debug_tick)
	{
		Color c;
		c = ARGBToColor(argb);
		ui_SliderAddSpecialValue(g_audio_dbg.setting_pane.debug_tick, gMaxFlowNodeTick-1, c);
	}
}

void sndProjectDebugMsgHandler(DebuggerObjectMsg *msg)
{
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			char *name = NULL;

			fmodEventProjectGetName(msg->obj_data, &name);
			strcpy_s(msg->getName.out.name, msg->getName.out.len, name);
		}
	}
}

void sndGroupDebugMsgHandler(DebuggerObjectMsg *msg)
{
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			char *name = NULL;

			fmodEventGroupGetName(msg->obj_data, &name);
			strcpy_s(msg->getName.out.name, msg->getName.out.len, name);
		}
	}
}

void sndEventDebugMsgHandler(DebuggerObjectMsg *msg)
{
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			char *name = NULL;

			fmodEventGetName(msg->obj_data, &name);
			strcpy_s(msg->getName.out.name, msg->getName.out.len, name);
		}
	}
}

void sndEventInstanceDebugMsgHandler(DebuggerObjectMsg *msg)
{
	SoundSource *source = msg->obj_data;
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			char *name = NULL;

			fmodEventGetName(source->info_event, &name);
			strcpy_s(msg->getName.out.name, msg->getName.out.len, name);
		}
	}
}

void sndSpaceMsgHandler(DebuggerObjectMsg *msg)
{
	SoundSpace *space = msg->obj_data;
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			if(space->obj.desc_name)
			{
				strcpy_s(msg->getName.out.name, msg->getName.out.len, space->obj.desc_name);
			}
			else
			{
				strcpy_s(msg->getName.out.name, msg->getName.out.len, "NULL");
			}
		}
	}
}

void sndSpaceConnectorDebugMsgHandler(DebuggerObjectMsg *msg)
{
	SoundSpaceConnector *conn = msg->obj_data;
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			strcpy_s(msg->getName.out.name, msg->getName.out.len, conn->obj.desc_name);
		}
	}
}

void sndConnectorTransmissionDebugMsgHandler(DebuggerObjectMsg *msg)
{
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			strcpy_s(msg->getName.out.name, msg->getName.out.len, "Trans");
		}
	}
}

void sndDebuggerDrawEvent(DebuggerType type, SoundSource *source, SoundSourceDebugPersistInfo *persist)
{
	Vec3 player_pos;

	sndGetPlayerPosition(player_pos);

	if(source)
	{
		int i;
		if(fmodEventIs2D(source->info_event))
			return;
		gfxDrawLine3DARGB(player_pos, source->virtual_pos, 0xFF00FF00);

		for(i=0; i<eaSize(&source->source_info.pathlegs); i++)
		{
			Vec3 min, max, off = {0.2, 0.2, 0.2};
			SoundSourceDebugPathLeg *leg = source->source_info.pathlegs[i];

			gfxDrawLine3DARGB(leg->line_start, leg->line_end, 0xFF00FF00);
			subVec3(leg->collision, off, min);
			addVec3(leg->collision, off, max);
			gfxDrawBox3DARGB(min, max, unitmat, 0xFFFF0000, 0);
		}
	}
	else if(persist)
	{
		if(fmodEventIs2D(persist->info_event))
			return;
		
		gfxDrawLine3DARGB(player_pos, persist->pos, 0xFFFF0000);
	}
}

void sndDebuggerDrawEvents(DebuggerType type, SoundSource *source, SoundSourceDebugPersistInfo *persist)
{
	// Callback for global event drawing, i.e. not the selected one
	F32 *p;
	Vec3 s, e;
	int i;
	int color = 0;

	if(type!=g_audio_dbg.instance_type)
	{
		return;
	}
	
	if(source)
	{
		if(fmodEventIs2D(source->info_event))
			return;
		p = source->virtual_pos;
		color = 0xFF00FF00;
	}
	else
	{
		if(fmodEventIs2D(persist->info_event))
			return;
		p = persist->pos;
		color = 0xFFFF0000;
	}

	for(i=0; i<3; i++)
	{
		copyVec3(p, s);
		copyVec3(p, e);

		s[i] += 0.2;	
		e[i] -= 0.2;

		gfxDrawLine3DARGB(s, e, color);
	}
}

void sndDebuggerDrawSpaces(DebuggerType type, void *space_conn, void *unused)
{
	if(type==g_audio_dbg.ss_type)
	{
		SoundSpace *space = (SoundSpace*)space_conn;

		sndDrawSpace(space, 0);
	}
	else if(type==g_audio_dbg.ssc_type)
	{
		SoundSpaceConnector *conn = (SoundSpaceConnector*)space_conn;

		sndDrawConn(conn, 0);
	}
}

void sndDebuggerAddPlayingEvents(void)
{
	int i;

	for(i=0; i<eaSize(&space_state.source_groups); i++)
	{
		ReferenceHandle *handle = NULL;
		SoundSourceGroup *group = space_state.source_groups[i];

		fmodEventGetUserData(group->fmod_info_event, (void**)&handle);
		devassert(handle);

		gDebuggerObjectAddVirtualObjectByHandle(handle, g_audio_dbg.group_type, "Active", &group->active_object.__handle_INTERNAL);
		gDebuggerObjectAddVirtualObjectByHandle(handle, g_audio_dbg.group_type, "Dead", &group->dead_object.__handle_INTERNAL);
		gDebuggerObjectAddVirtualObjectByHandle(handle, g_audio_dbg.group_type, "Inactive", &group->inactive_object.__handle_INTERNAL);
	}

	for(i=0; i<eaSize(&space_state.sources); i++)
	{
		SoundSource *source = space_state.sources[i];
		DebuggerObject *parent = NULL;

		if(source->has_event)
		{
			gDebuggerObjectAddObject(source->group->active_object, 
										g_audio_dbg.instance_type, 
										source, source->debug_object);
		}
		else 
		{
			if(source->dead)
			{
				gDebuggerObjectAddObject(source->group->dead_object, 
										g_audio_dbg.instance_type, 
										source, source->debug_object);
			}
			else
			{
				gDebuggerObjectAddObject(source->group->inactive_object, 
										g_audio_dbg.instance_type, 
										source, source->debug_object);
			}
		}

		gDebuggerObjectAddChild(source->originSpace->debug_object, source->debug_object);

		if(source->has_event)
		{
			gDebuggerObjectAddFlag(source->debug_object, g_audio_dbg.event_playing);
		}
	}
}

U32 sndDebuggerIsObjectType(DebuggerType type)
{
	return type==g_audio_dbg.instance_type || type==g_audio_dbg.ss_type || type==g_audio_dbg.ssc_type;
}

void sndDebuggerSetObjectVolume(ReferenceHandle *object, DebuggerType type, SoundObject *obj, SoundSourceDebugPersistInfo *persist, F32 value, U32 inherited)
{
	UIPane *pane = NULL;
	DebugUserPane *soundpane = NULL;

	pane = gDebuggerObjectGetUserPaneByHandle(object);
	if(stashAddressFindPointer(snd_userpane_stash, pane, &soundpane))
	{
		if(!inherited)
		{
			soundpane->volume_slider->widget.pOverrideSkin = g_audio_dbg.vol_set_skin;
			soundpane->reset->widget.state = kWidgetModifier_None;
		}
		else
		{
			soundpane->volume_slider->widget.pOverrideSkin = NULL;
			soundpane->reset->widget.state = kWidgetModifier_Inactive;
		}

		ui_FloatSliderSetValueAndCallbackEx(soundpane->volume_slider, 0, value, 0);
	}

	if(sndDebuggerIsObjectType(type) && obj)
	{
		obj->debug_volume = value;
		obj->debug_volume_set = 1;
	}
}

void sndDebuggerRebuildEventTree(void)
{
	gDebuggerObjectRemoveChildren(g_audio_dbg.events);

	FMOD_EventSystem_TreeTraverse(STT_PROJECT, sndBuildEventTree, &g_audio_dbg.events.__handle_INTERNAL, NULL);
}

FMOD_RESULT __stdcall sndDebuggerDSPConnectionMixRampCallback(void *dspconn, float *buffer, int channels, int length)
{
	return FMOD_OK;
}

FMOD_RESULT __stdcall sndDebuggerDSPExecuteCallback(void *dsp, float *buffer, int channels, int length)
{
	DSPFlowNodeSnapShot *snapshot = NULL;
	DSPFlowNode *flownode = NULL;
	
	if(!g_audio_dbg.debug_DSP_flow)
	{
		return FMOD_OK;
	}
	if(!dsp)
	{
		return FMOD_OK;
	}

	if(!stashAddressFindPointer(snd_flownode_stash, dsp, &flownode))
	{
		return FMOD_OK;
	}

	if(flownode->touched)
	{
		return FMOD_OK;
	}

	flownode->touched = 1;
	snapshot = callocStruct(DSPFlowNodeSnapShot);
	snapshot->buffer = callocStructs(float, length);
	memcpy(snapshot->buffer, buffer, length*sizeof(float));
	snapshot->length = length;
	snapshot->channels = channels;

	EnterCriticalSection(&flow_node_crit);
	{
		eaPush(&flownode->history, snapshot);

		if(flownode->dspo==DSPO_System)
		{
			// Increment time stamp for self and others
			StashTableIterator iter;
			StashElement elem;

			stashGetIterator(snd_flownode_stash, &iter);
			while(stashGetNextElement(&iter, &elem))
			{
				DSPFlowNode *otherflownode = stashElementGetPointer(elem);
				DSPFlowNodeSnapShot *othersnapshot = eaTail(&otherflownode->history);

				if(othersnapshot && !othersnapshot->timestamp)
				{
					devassert(!othersnapshot->timestamp);
					othersnapshot->timestamp = gMaxFlowNodeTick;
					otherflownode->touched = 0;
				}
			}

			gMaxFlowNodeTick++;
		}
	}	
	LeaveCriticalSection(&flow_node_crit);

	return FMOD_OK;
}

StashTable allocated_dsp_stash;
CRITICAL_SECTION cs_dspexec;

FMOD_RESULT __stdcall sndDSPAllocCallback(void *dsp)
{
	EnterCriticalSection(&cs_dspexec);

	stashAddressRemoveInt(allocated_dsp_stash, dsp, NULL);

	LeaveCriticalSection(&cs_dspexec);

	return FMOD_OK;
}

FMOD_RESULT __stdcall sndDSPFreeCallback(void *dsp)
{
	EnterCriticalSection(&cs_dspexec);

	stashAddressAddInt(allocated_dsp_stash, dsp, 1, 1);

	LeaveCriticalSection(&cs_dspexec);

	return FMOD_OK;
}

FMOD_RESULT __stdcall sndDSPExecuteCallback(void *dsp, float *buffer, int channels, int length)
{
	EnterCriticalSection(&cs_dspexec);

	if(stashAddressFindInt(allocated_dsp_stash, dsp, NULL))
		devassertmsg(0, "Trying to execute freed DSP");

	LeaveCriticalSection(&cs_dspexec);

	return sndDebuggerDSPExecuteCallback(dsp, buffer, channels, length);
}

void sndDebugSetCallbacks(void)
{
	if(!allocated_dsp_stash)
		allocated_dsp_stash = stashTableCreateAddress(10);

	InitializeCriticalSection(&cs_dspexec);

// disabled:(GT)
//#if !_PS3 // TODO(am): Implement for FMOD PS3
//	fmodEventSystemSetDSPExecuteCallback(sndDSPExecuteCallback);
//	fmodEventSystemSetDSPAllocCallback(sndDSPAllocCallback);
//	fmodEventSystemSetDSPFreeCallback(sndDSPFreeCallback);
//#endif

}

void sndDebuggerInit(void)
{
	static int crit_init = 0;
	Color red = {0xFF, 0x00, 0x00, 0xFF};
	void *dsphead = NULL;
	Vec2 lb = {0, -1};
	Vec2 ub = {200, 1};

	if(!crit_init)
	{
		crit_init = 1;
		InitializeCriticalSection(&flow_node_crit);
	}

//#if !_PS3 // TODO(am): Implement for FMOD PS3
//	fmodEventSystemSetDSPExecuteCallback(sndDebuggerDSPExecuteCallback);
//#endif

	g_audio_dbg.vol_set_skin = ui_SkinCreate(NULL);
	ui_SkinSetButton(g_audio_dbg.vol_set_skin, red);

	g_audio_dbg.project_type	= gDebugRegisterDebugObjectType("AudioProject", sndProjectDebugMsgHandler);
	g_audio_dbg.group_type		= gDebugRegisterDebugObjectType("AudioGroup", sndGroupDebugMsgHandler);
	g_audio_dbg.event_type		= gDebugRegisterDebugObjectType("AudioEvent", sndEventDebugMsgHandler);
	g_audio_dbg.instance_type	= gDebugRegisterDebugObjectType("AudioEventInstance", sndEventInstanceDebugMsgHandler);
	g_audio_dbg.ss_type			= gDebugRegisterDebugObjectType("SoundSpace", sndSpaceMsgHandler);
	g_audio_dbg.ssc_type		= gDebugRegisterDebugObjectType("SoundSpaceConnector", sndSpaceConnectorDebugMsgHandler);
	g_audio_dbg.ssct_type		= gDebugRegisterDebugObjectType("SoundSpaceConnectorTransmission", sndConnectorTransmissionDebugMsgHandler);

	gDebuggerTypeCreateGroup(g_audio_dbg.ss_type);
	gDebuggerTypeAddToGroup(g_audio_dbg.ss_type, g_audio_dbg.ssc_type);
	gDebuggerTypeAddToGroup(g_audio_dbg.ss_type, g_audio_dbg.ssct_type);

	gDebuggerTypeSetDrawFunc(g_audio_dbg.instance_type, sndDebuggerDrawEvent);

	g_audio_dbg.event_playing	= gDebugRegisterDebugObjectFlag("AudioEventPlaying", 1, 0);
	g_audio_dbg.event_played	= gDebugRegisterDebugObjectFlag("AudioEventPlayed", 1, 0);
	g_audio_dbg.space_active	= gDebugRegisterDebugObjectFlag("AudioSpaceActive", 0, 1);
	g_audio_dbg.volume_setting	= gDebugRegisterDebugObjectValueFlag("AudioVolume", 0, 1, 1, 0, 0);

	gDebuggerFlagSetChangedCallback(g_audio_dbg.volume_setting, sndDebuggerSetObjectVolume);

	gDebuggerAddRoot(g_audio_dbg.dbger, "PlayingEvents", sndDebuggerDrawEvents, g_audio_dbg.events);
	gDebuggerRootAddType(g_audio_dbg.events, g_audio_dbg.project_type);
	gDebuggerRootAddType(g_audio_dbg.events, g_audio_dbg.group_type);
	gDebuggerRootAddType(g_audio_dbg.events, g_audio_dbg.event_type);
	gDebuggerRootAddType(g_audio_dbg.events, g_audio_dbg.instance_type);

	gDebuggerAddRoot(g_audio_dbg.dbger, "SpaceTree", sndDebuggerDrawSpaces, g_audio_dbg.spaces);
	gDebuggerRootAddType(g_audio_dbg.spaces, g_audio_dbg.ss_type);
	gDebuggerRootAddType(g_audio_dbg.spaces, g_audio_dbg.ssc_type);
	gDebuggerRootAddType(g_audio_dbg.spaces, g_audio_dbg.ssct_type);
	gDebuggerRootAddType(g_audio_dbg.spaces, g_audio_dbg.instance_type);

	sndDebuggerRebuildEventTree();

	if(!snd_flownode_stash)
	{
		snd_flownode_stash = stashTableCreateAddress(20);
	}

	if(eaSize(&space_state.global_spaces))
	{
		int i;
		for(i=eaSize(&space_state.global_spaces)-1; i>=0; i--)
		{
			SoundSpace *space = space_state.global_spaces[i];

			gDebuggerObjectAddObject(g_audio_dbg.spaces, g_audio_dbg.ss_type, space, space->debug_object);

			if(space->is_current)
			{
				gDebuggerObjectAddFlag(space->debug_object, g_audio_dbg.space_active);
			}
		}
	}

	if(eaSize(&space_state.global_conns))
	{
		int i;
		for(i=eaSize(&space_state.global_conns)-1; i>=0; i--)
		{
			SoundSpaceConnector *conn = space_state.global_conns[i];

			gDebuggerObjectAddObject(g_audio_dbg.spaces, g_audio_dbg.ssc_type, conn, conn->debug_object);
		}
	}

	sndDebuggerAddPlayingEvents();

	space_state.needs_reconnect = 1;

	g_audio_dbg.debugging = 1;
}

static void sndDebuggerReload(UIAnyWidget *widget, UserData unused)
{
	g_audio_dbg.reload = 1;
}

void sndUpdateAudioSettingsArea(void)
{
	int cur, mem;
	F32 cpu;
	char str[128];
	int i;
	U32 offsetPos;
	F32 gain;
	F32 val;

	FMOD_EventSystem_GetMemStats(&cur, &mem);
	cpu = fmodGetCPUUsage()/100;

	MINMAX1(cpu, 0, 1);
	MINMAX1(mem, 0, soundBufferSize);

	// Memory
	//ui_ProgressBarSet(g_audio_dbg.setting_pane.memory, (F32)cur/soundBufferSize);
	val = CLAMP( (F32)cur/soundBufferSize, 0.0, 1.0 );

	ui_ProgressBarSet(g_audio_dbg.setting_pane.memory, val);
	sprintf(str, "%d of %d (max allocated: %d)", cur, soundBufferSize, mem);
	ui_LabelSetText(g_audio_dbg.setting_pane.memory_info_label, str);

	// CPU
	if(cpu > g_audio_dbg.setting_pane.max_cpu_value) g_audio_dbg.setting_pane.max_cpu_value = cpu;

	ui_ProgressBarSet(g_audio_dbg.setting_pane.cpu, cpu);
	sprintf(str, "%f (max: %f)", cpu, g_audio_dbg.setting_pane.max_cpu_value);
	
	
	ui_LabelSetText(g_audio_dbg.setting_pane.cpu_value_label, str);

	// Current Space
	if(space_state.current_space && space_state.current_space->obj.desc_name)
	{
		SoundSpace *soundSpace;
		SoundDSP *soundDSP;
		//void *fmod_dsp;
		F32 connectionVolume;
		char spaceStr[128];
		soundSpace = space_state.current_space;
		soundDSP = GET_REF(soundSpace->dsp_ref);

		connectionVolume = fmodDSPConnectionGetVolume(soundSpace->obj.original_conn);

		if(soundDSP) {
			bool bypass = false;

			sprintf(spaceStr, "%s [%s] vol:%.3f", soundSpace->obj.desc_name, soundDSP->name, connectionVolume);
		
			//fmod_dsp = fmodChannelGroupGetDSPHead(soundSpace->obj.fmod_dsp_unit);
			//fmodDSPGetBypass(fmod_dsp, &bypass);

			fmodDSPGetBypass(soundSpace->obj.fmod_dsp_unit, &bypass);
			
			ui_CheckButtonSetState(g_audio_dbg.setting_pane.bypass_dsp, bypass);
			ui_CheckButtonSetToggledCallback(g_audio_dbg.setting_pane.bypass_dsp, sndToggleDSPSpaceBypass, (void*)soundSpace);

		} else {

			sprintf(spaceStr, "%s vol:%.3f", soundSpace->obj.desc_name, connectionVolume);
		}
		ui_LabelSetText(g_audio_dbg.setting_pane.current_space, spaceStr);
	} 
	else 
	{
		ui_LabelSetText(g_audio_dbg.setting_pane.current_space, "");
	}

	// Current Music
	offsetPos = 0;
	ui_TextAreaSetText(g_audio_dbg.setting_pane.last_music, "");

	for(i=0; i<eaSize(&music_state.playing); i++)
	{
		SoundSource *me = music_state.playing[i];

		FMOD_EventSystem_GetVolume(me->fmod_event, &gain);

		sprintf(str, "%s %s[vol=%.3f xfade=%.3f]\n", me->obj.desc_name, me->is_muted ? "[M] " : "", gain, me->fade_level);
		
		ui_TextAreaInsertTextAt(g_audio_dbg.setting_pane.last_music, offsetPos, str);
		offsetPos += (U32) strlen(str);
	}
	
	

}

void sndToggleDSPDebug(UIAnyWidget *widget, UserData unused)
{
	int state = ui_CheckButtonGetState(g_audio_dbg.setting_pane.debug_dsp);

	g_audio_dbg.debug_DSP_flow = !!state;

	if(!g_audio_dbg.debug_DSP_flow)
	{
		// Clear out histories and such
	}

	ui_SliderAddSpecialValue(g_audio_dbg.setting_pane.debug_tick, gMaxFlowNodeTick-1, ColorGreen);
}

void sndToggleDSPSpaceBypass(UIAnyWidget *widget, UserData unused)
{
	SoundSpace *soundSpace = (SoundSpace*)unused;
	int state = ui_CheckButtonGetState((UICheckButton*)widget);
		//g_audio_dbg.setting_pane.bypass_dsp);

	if(soundSpace)
	{
		//void *fmod_dsp;

		//fmod_dsp = fmodChannelGroupGetDSPHead(soundSpace->obj.fmod_dsp_unit);
		fmodDSPSetBypass(soundSpace->obj.fmod_dsp_unit, state > 0 ? true : false);
	}
}

void sndSetViewTick(UIAnyWidget *widget, bool bFinished, UserData unused)
{
	gFlowNodeTick = ui_IntSliderGetValue(g_audio_dbg.setting_pane.debug_tick);
}

void sndPopulateAudioSettingsArea(UIScrollArea *area)
{
	F32 col_1_x = 7;
	F32 col_2_x = 107.0;
	F32 line_y = 2;
	F32 y_spacing = 1;
	F32 x;
	UITextArea *textArea;

	// Reload + DSP
	g_audio_dbg.setting_pane.reload = ui_ButtonCreate("Reload", col_1_x, line_y, sndDebuggerReload, NULL);
	g_audio_dbg.setting_pane.debug_dsp = ui_CheckButtonCreate(col_2_x, 0, "DSP Debug", 0);
	g_audio_dbg.setting_pane.debug_tick = ui_IntSliderCreate(ui_WidgetGetNextX(UI_WIDGET(g_audio_dbg.setting_pane.debug_dsp))+5, line_y, 100, 0, 0, 0);

	ui_CheckButtonSetToggledCallback(g_audio_dbg.setting_pane.debug_dsp, sndToggleDSPDebug, NULL);
	ui_SliderSetChangedCallback(g_audio_dbg.setting_pane.debug_tick, sndSetViewTick, NULL);
	ui_SliderSetPolicy(g_audio_dbg.setting_pane.debug_tick, UISliderContinuous);

	line_y = ui_WidgetGetNextY(UI_WIDGET(g_audio_dbg.setting_pane.reload)) + y_spacing;

	// Memory Info
	g_audio_dbg.setting_pane.memory_label = ui_LabelCreate("Memory", col_1_x, line_y);
	g_audio_dbg.setting_pane.memory = ui_ProgressBarCreate(col_2_x, line_y, 100);
	x = ui_WidgetGetNextX(UI_WIDGET(g_audio_dbg.setting_pane.memory)) + 5;
	g_audio_dbg.setting_pane.memory_info_label = ui_LabelCreate("0 of 0", x, line_y);

	line_y = ui_WidgetGetNextY(UI_WIDGET(g_audio_dbg.setting_pane.memory_label)) + y_spacing;

	// CPU
	g_audio_dbg.setting_pane.cpu_label = ui_LabelCreate("CPU", col_1_x, line_y);
	g_audio_dbg.setting_pane.cpu = ui_ProgressBarCreate(col_2_x, line_y, 100);
	x = ui_WidgetGetNextX(UI_WIDGET(g_audio_dbg.setting_pane.cpu)) + 5;
	g_audio_dbg.setting_pane.cpu_value_label = ui_LabelCreate("0%", x, line_y);
	g_audio_dbg.setting_pane.max_cpu_value = 0.0;

	line_y = ui_WidgetGetNextY(UI_WIDGET(g_audio_dbg.setting_pane.cpu_label)) + y_spacing;

	// Current Space
	g_audio_dbg.setting_pane.current_space_label = ui_LabelCreate("Current Space", col_1_x, line_y);
	g_audio_dbg.setting_pane.bypass_dsp = ui_CheckButtonCreate(col_2_x, line_y, "Bypass DSP", 0);
	g_audio_dbg.setting_pane.current_space = ui_LabelCreate("(None)", col_2_x + 100, line_y);

	line_y = ui_WidgetGetNextY(UI_WIDGET(g_audio_dbg.setting_pane.current_space_label)) + y_spacing;

	// Last Music
	

	g_audio_dbg.setting_pane.last_music_label = ui_LabelCreate("Playing Music", col_1_x, line_y);
	textArea = ui_TextAreaCreate("Music"); 
	ui_WidgetSetPosition(UI_WIDGET(textArea), col_2_x, line_y);
	ui_WidgetSetDimensions(UI_WIDGET(textArea), 700, 100);
	g_audio_dbg.setting_pane.last_music = textArea;
	
	
	



	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.reload));

	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.memory_label));
	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.memory));
	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.memory_info_label));

	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.cpu_label));
	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.cpu));
	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.cpu_value_label));

	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.debug_dsp));
	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.debug_tick));

	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.current_space_label));
	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.bypass_dsp));
	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.current_space));

	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.last_music_label));
	ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.setting_pane.last_music));
}

void sndDebuggerSourceGetPos(char **estrOut)
{
	if(g_audio_dbg.debugged_source)
	{
		estrPrintf(estrOut, LOC_PRINTF_STR, vecParamsXYZ(g_audio_dbg.debugged_source->virtual_pos));
	}
	else if(g_audio_dbg.debugged_persist)
	{
		estrPrintf(estrOut, LOC_PRINTF_STR, vecParamsXYZ(g_audio_dbg.debugged_persist->pos));
	}
}

void sndDebuggerSourceGetDir(char **estrOut)
{
	if(g_audio_dbg.debugged_source)
	{
		estrPrintf(estrOut, LOC_PRINTF_STR, vecParamsXYZ(g_audio_dbg.debugged_source->virtual_dir));
	}
	else if(g_audio_dbg.debugged_persist)
	{
		estrPrintf(estrOut, LOC_PRINTF_STR, vecParamsXYZ(g_audio_dbg.debugged_persist->dir));
	}
}

void sndDebuggerSourceGetDist(char **estrOut)
{
	if(g_audio_dbg.debugged_source)
	{
		if(g_audio_dbg.debugged_source->has_event)
		{
			F32 radius = 0;
			radius = fmodEventGetMaxRadius(g_audio_dbg.debugged_source->fmod_event);
			estrPrintf(estrOut, "dist=%f radius=%f", g_audio_dbg.debugged_source->distToListener, radius);
		}
		else if(g_audio_dbg.debugged_info)
		{
			estrPrintf(estrOut, "radius=%f", fmodEventGetMaxRadius(g_audio_dbg.debugged_info));
		}
	}
	else if(g_audio_dbg.debugged_persist)
	{
		estrPrintf(estrOut, "dist=%f radius=%f", g_audio_dbg.debugged_persist->dist, g_audio_dbg.debugged_persist->radius);
	}
}

void sndDebuggerSourceGetVel(char **estrOut)
{
	if(g_audio_dbg.debugged_source)
	{
		if(g_audio_dbg.debugged_source->type==ST_POINT)
		{
			estrPrintf(estrOut, LOC_PRINTF_STR, vecParamsXYZ(g_audio_dbg.debugged_source->point.vel));
		}
		else
		{
			estrPrintf(estrOut, LOC_PRINTF_STR, 0.0, 0.0, 0.0);
		}
	}
	else if(g_audio_dbg.debugged_persist)
	{
		estrPrintf(estrOut, LOC_PRINTF_STR, vecParamsXYZ(g_audio_dbg.debugged_persist->vel));
	}
}

void sndDebuggerSourceGetVol(char **estrOut)
{
	if(g_audio_dbg.debugged_source && g_audio_dbg.debugged_source->has_event)
	{
		estrPrintf(estrOut, "%f", fmodEventGetVolume(g_audio_dbg.debugged_source->fmod_event));
	}
	else
	{
		estrPrintf(estrOut, "Not Playing");
	}
}

void sndDebuggerSourceGetBaseVol(char **estrOut)
{
	if(g_audio_dbg.debugged_source)
	{
		estrPrintf(estrOut, "%f", fmodEventGetVolume(g_audio_dbg.debugged_source->info_event));
	}
	else if(g_audio_dbg.debugged_info)
	{
		estrPrintf(estrOut, "%f", fmodEventGetVolume(g_audio_dbg.debugged_info));
	}
}

void sndDebuggerSourceGetPan(char **estrOut)
{
	if(g_audio_dbg.debugged_source && g_audio_dbg.debugged_source->has_event)
		estrPrintf(estrOut, "dir=%.2f level=%.2f", g_audio_dbg.debugged_source->directionality, fmodEventGetPanLevel(g_audio_dbg.debugged_source->fmod_event));
	else if(g_audio_dbg.debugged_info)
		estrPrintf(estrOut, "level=%f", fmodEventGetPanLevel(g_audio_dbg.debugged_info));
}

void sndDebuggerSourceGetMode(char **estrOut)
{
	int is2D = 0;
	if(!g_audio_dbg.debugged_info)
	{
		estrPrintf(estrOut, "No event selected");
		return;
	}
	
	is2D = fmodEventIs2D(g_audio_dbg.debugged_info);

	if(is2D)
		estrPrintf(estrOut, "2D");
	else
		estrPrintf(estrOut, "3D");
}

void sndDebuggerSourceGetPlaybacks(char **estrOut)
{
	if(g_audio_dbg.debugged_info)
		estrPrintf(estrOut, "%d", fmodEventGetMaxPlaybacks(g_audio_dbg.debugged_info));
}

void sndDebuggerSourceGetOneshot(char **estrOut)
{
	if(g_audio_dbg.debugged_info)
		estrPrintf(estrOut, "%s", !fmodEventIsLooping(g_audio_dbg.debugged_info) ? "Yes" : "No");
}

void sndDebuggerSourceGetFadeIn(char **estrOut)
{
	F32 fadeIn = 0;

	estrPrintf(estrOut, "Unsupported");
}

void sndDebuggerSourceGetFadeOut(char **estrOut)
{
	F32 fadeOut = 0;

	estrPrintf(estrOut, "Unsupported");
}

void sndDebuggerSourceGetDoppler(char **estrOut)
{
	if(g_audio_dbg.debugged_info)
	{
		estrPrintf(estrOut, "%f", fmodEventGetDopplerScale(g_audio_dbg.debugged_info));
	}
	else
	{
		estrPrintf(estrOut, "No Debug Info");
	}
}

void sndDebuggerPrintVarName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	estrClear(estrOutput);
	
	if(g_audio_dbg.info_pane.info_list)
	{
		if(iRow<ssdlMax)
		{
			estrPrintf(estrOutput, "%s", StaticDefineIntRevLookup(SoundSourceDebugListEnum, iRow));
		}
	}
}

void sndDebuggerPrintVarVal(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	estrClear(estrOutput);

	if(g_audio_dbg.info_pane.info_list)
	{
		if(iRow<ssdlMax)
		{
			if(iRow<ARRAY_SIZE(sndSourceDebugFuncs))
				sndSourceDebugFuncs[iRow](estrOutput);
			else
				estrPrintf(estrOutput, "No Var Func");
		}
	}
}

void sndPopulateAudioInfoArea(DebuggerType type, UIScrollArea *area, void *data, SoundSourceDebugPersistInfo *persist, int initial)
{
	SoundObject *sound_obj = NULL;
	static void **fakeVarEArray = NULL;
	SoundSpace *soundSpace = NULL;
	SoundDSP *soundDSP = NULL;

	if(type==g_audio_dbg.instance_type || type==g_audio_dbg.ss_type || type==g_audio_dbg.ssc_type)
	{
		sound_obj = (SoundObject*)data;

		if(type==g_audio_dbg.ss_type) {
			soundSpace = (SoundSpace*)sound_obj;
			soundDSP = GET_REF(soundSpace->dsp_ref);
		}
	}

	if(initial)
	{
		// Initial
		int i;
		UIListColumn *col;
		eaSetSize(&fakeVarEArray, ssdlMax);

		for(i=0; eaSize(&fakeVarEArray)<ssdlMax; i++)
		{
			eaPush(&fakeVarEArray, NULL);
		}
		g_audio_dbg.info_pane.info_list = ui_ListCreate(NULL, &fakeVarEArray, 15);
		ui_WidgetSetClickThrough(UI_WIDGET(g_audio_dbg.info_pane.info_list), 1);
		
		col = ui_ListColumnCreate(UIListTextCallback, "Var", (intptr_t)sndDebuggerPrintVarName, NULL);
		ui_ListAppendColumn(g_audio_dbg.info_pane.info_list, col);
		col = ui_ListColumnCreate(UIListTextCallback, "Val", (intptr_t)sndDebuggerPrintVarVal, NULL);
		ui_ListAppendColumn(g_audio_dbg.info_pane.info_list, col);

		ui_WidgetSetPosition(UI_WIDGET(g_audio_dbg.info_pane.info_list), 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(g_audio_dbg.info_pane.info_list), 1.0, 15*(ssdlMax+1), UIUnitPercentage, UIUnitFixed);

		ui_ScrollAreaAddChild(area, UI_WIDGET(g_audio_dbg.info_pane.info_list));
	}
	else
	{
		if(type==g_audio_dbg.instance_type)
		{
			g_audio_dbg.debugged_source = (SoundSource*)sound_obj;
			g_audio_dbg.debugged_persist = persist;

			if(g_audio_dbg.debugged_source)
				g_audio_dbg.debugged_info = g_audio_dbg.debugged_source->info_event;
			else if(g_audio_dbg.debugged_persist)
				g_audio_dbg.debugged_info = g_audio_dbg.debugged_persist->info_event;
			ui_WidgetSetHeight(UI_WIDGET(g_audio_dbg.info_pane.info_list), 15*(ssdlMax+1));
		}
		else if(type==g_audio_dbg.event_type)
		{
			g_audio_dbg.debugged_info = data;
			g_audio_dbg.debugged_source = NULL;
			g_audio_dbg.debugged_persist = NULL;
			ui_WidgetSetHeight(UI_WIDGET(g_audio_dbg.info_pane.info_list), 15*(ssdlMax+1));
		}
		else
		{
			ui_WidgetSetHeight(UI_WIDGET(g_audio_dbg.info_pane.info_list), 0);
		}
	}
}

void sndDebuggerObjectVolumeChanged(UIAnyWidget *widget, bool bFinished, DebugUserPane *userpane)
{
	F32 value;

	value = ui_FloatSliderGetValue(userpane->volume_slider);
	gDebuggerObjectSetFlag(userpane->debug_object, g_audio_dbg.volume_setting, value);
}

void sndFreeDebugPane(UIPane *pane)
{
	DebugUserPane *soundpane = NULL;

	if(!snd_userpane_stash)
	{
		// Really weird
		return;
	}

	if(stashAddressRemovePointer(snd_userpane_stash, pane, &soundpane))
	{
		REMOVE_HANDLE(soundpane->debug_object);
		free(soundpane);
	}
	else
	{
		devassert(0);
	}
}

void sndDebuggerObjectResetVolume(UIAnyWidget *widget, DebugUserPane *pane)
{
	gDebuggerObjectResetFlag(pane->debug_object, g_audio_dbg.volume_setting, 0);
}

static int sndDebugCmpSnapshot(const DSPFlowNodeSnapShot **sn1, const DSPFlowNodeSnapShot **sn2)
{
	return ((*sn1)->timestamp > (*sn2)->timestamp) ? 1 : (((*sn1)->timestamp < (*sn2)->timestamp) ? -1 : 0);
}

DSPFlowNodeSnapShot* sndDebugFlowNodeFindTick(DSPFlowNode *node, int tick)
{
	DSPFlowNodeSnapShot dummy = {0};
	DSPFlowNodeSnapShot *dummy_ptr = &dummy;
	DSPFlowNodeSnapShot **shotref = NULL;
	
	dummy.timestamp = tick;
	shotref = eaBSearch(node->history, sndDebugCmpSnapshot, dummy_ptr);
	return shotref ? *shotref : NULL;
}

void sndDebugFillBarGraph(UIBarGraph *graph, DSPFlowNode *node, int tick)
{
	DSPFlowNodeSnapShot *shot = sndDebugFlowNodeFindTick(node, tick);
	if(shot)
		ui_BarGraphSetModel(graph, shot->buffer, shot->channels, shot->length, 1);
	else
		ui_BarGraphSetModel(graph, NULL, 0, 0, 1);
}

void sndDebugProcessDSP(UIPane *pane, UIWidget *prevwidget, UIBarGraph **graphInOut, void *dsp, DSPFlowNode **flownodeInOut, SoundObject *obj, U32 system)
{
	DSPFlowNode *flownode = *flownodeInOut;
	Vec2 ll = {0, -1};
	Vec2 ul = {200, 1};
	UIBarGraph *graph = *graphInOut;

	if(!graph)
	{
		graph = ui_BarGraphCreate(NULL, NULL, ll, ul);
		ui_WidgetSetPosition(	UI_WIDGET(graph), 
								ui_WidgetGetNextX(prevwidget)+UI_HSTEP, 
								0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(graph), 100, 100.0, UIUnitFixed, UIUnitFixed);
		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(graph));

		*graphInOut = graph;
	}

	if(flownode && dsp)
	{
		stashAddressAddPointer(snd_flownode_stash, dsp, flownode, 1);
		sndDebugFillBarGraph(graph, flownode, gFlowNodeTick);
	}
	else if(dsp)
	{
		flownode = callocStruct(DSPFlowNode);
		flownode->dspo = system ? DSPO_System : DSPO_Other;
		flownode->origin_data = obj;
		stashAddressAddPointer(snd_flownode_stash, dsp, flownode, 1);

		*flownodeInOut = flownode;
	}
}

void sndDebuggerObjectPlay(UIAnyWidget *widget, DebugUserPane *soundpane)
{
	UIButton *b = (UIButton*)widget;

	if(!soundpane->testsource)
	{
		Vec3 pos;
		void *info_event = gDebuggerObjectGetData(soundpane->debug_object);
		char *str = NULL;

		fmodEventGetFullName(&str, info_event, true);
		
		sndGetListenerPosition(pos);
		soundpane->testsource = sndSourceCreate(__FILE__, NULL, str, pos, ST_POINT, SO_REMOTE, NULL, -1, false);

		if(!soundpane->testsource)
			return;

		ui_ButtonSetText(soundpane->playtest, "Stop");
		estrDestroy(&str);
	}
	else
	{
		soundpane->testsource->needs_stop = 1;
		soundpane->testsource = NULL;

		ui_ButtonSetText(soundpane->playtest, "Play");
	}
}

void sndPopulateAudioUserPane(ReferenceHandle *obj, DebuggerType type, void *data, SoundSourceDebugPersistInfo *persist, UIPane *pane)
{
	DebugUserPane *soundpane = NULL;
	SoundObject *sound_obj = NULL;
	DebuggerType debuggerType;

	if(type==g_audio_dbg.instance_type || type==g_audio_dbg.ss_type || type==g_audio_dbg.ssc_type)
	{
		sound_obj = (SoundObject*)data;
	}

	if(!snd_userpane_stash)
	{
		snd_userpane_stash = stashTableCreateAddress(10);
	}

	stashAddressFindPointer(snd_userpane_stash, pane, &soundpane);
	if(!soundpane)
	{
		F32 x = 0, y = 0;
		Vec2 ll = {0, -1};
		Vec2 ul = {200, 1};
		bool addBypassDSP = false;

		devassert(!soundpane);
		soundpane = callocStruct(DebugUserPane);
		RefSystem_CopyHandle(&soundpane->debug_object.__handle_INTERNAL, obj);

		debuggerType = gDebuggerObjectGetType(soundpane->debug_object);

		if(debuggerType == g_audio_dbg.event_type)
		{
			soundpane->playtest = ui_ButtonCreate("Play", 0, 0, sndDebuggerObjectPlay, soundpane);
			x = ui_WidgetGetNextX(UI_WIDGET(soundpane->playtest))+UI_HSTEP;
		}

		soundpane->reset = ui_ButtonCreate("ResetVol", x, 0, sndDebuggerObjectResetVolume, soundpane);
		x = ui_WidgetGetNextX(UI_WIDGET(soundpane->reset))+UI_HSTEP;
		soundpane->reset->widget.state = kWidgetModifier_Inactive;
		soundpane->volume_slider = ui_FloatSliderCreate(x, 0, 100, 0, 1, 1);
		x = ui_WidgetGetNextX(UI_WIDGET(soundpane->volume_slider))+UI_HSTEP;
		soundpane->memory_usage = ui_LabelCreate("0/0", x, 0);

		ui_WidgetSetFreeCallback(UI_WIDGET(pane), sndFreeDebugPane);
		ui_SliderSetChangedCallback(soundpane->volume_slider, sndDebuggerObjectVolumeChanged, soundpane);

		if(soundpane->playtest)
			ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(soundpane->playtest));
		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(soundpane->reset));
		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(soundpane->volume_slider));
		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(soundpane->memory_usage));
		stashAddressAddPointer(snd_userpane_stash, pane, soundpane, 1);
	}
	else
	{
		F32 volume = gDebuggerObjectGetFlagValue(soundpane->debug_object, g_audio_dbg.volume_setting);

		if(gDebuggerObjectHasFlag(soundpane->debug_object, g_audio_dbg.volume_setting))
		{
			ui_SliderSetValueAndCallbackEx(soundpane->volume_slider, 0, volume, 0, 0);
		}
	}


	if(sound_obj || persist)
	{
		char memory[MAX_PATH];
		int mem = 0, maxmem = 0;

		if(type==g_audio_dbg.instance_type)
		{
			if(sound_obj)
			{
				SoundSource *source = (SoundSource*)sound_obj;

				mem = source->memory_usage;
				maxmem = source->memory_usage_max;
			}
			else if(persist)
			{
				maxmem = persist->memory_usage_max;
			}
		}
		sprintf(memory, "%d/%d", mem, maxmem);
		ui_LabelSetText(soundpane->memory_usage, memory);
	}
}

U32 sndBuildEventTree(char *name, void *e_g_c, SoundTreeType type, void *p_userdata, void *userdata, void **new_p_userdata)
{
	DebuggerType d_type = 0;
	ReferenceHandle *handle = NULL;
	if(type==STT_EVENT_INSTANCE)
	{
		return 0;
	}

	handle = calloc(1, sizeof(ReferenceHandle*));
	*handle = calloc(1, sizeof(ReferenceHandle));

	switch(type)
	{
		xcase STT_PROJECT:  {
			d_type = g_audio_dbg.project_type;
		}
		xcase STT_GROUP: {
			d_type = g_audio_dbg.group_type;
		}
		xcase STT_EVENT: {
			d_type = g_audio_dbg.event_type;
		}
	}

	gDebuggerObjectAddObjectByHandle(p_userdata, d_type, e_g_c, handle);

	*new_p_userdata = handle;

	if(type==STT_EVENT) 
	{
		// gt: disabled wanted to store SoundSource in userData
		//fmodEventSetUserData(e_g_c, *new_p_userdata);
	}

	return 1;
}

void sndDebuggerTick(void)
{
	void *dsp = NULL;
	DSPFlowNode *flownode = NULL;
	static int lastEditorActive = 0;
	int editorActive = 0;

	if(g_audio_dbg.reload)
	{
		g_audio_dbg.reload = 0;

		g_audio_dbg.setting_pane.max_cpu_value = 0.0; // reset the max

		sndReloadAll("Sound Data");
		sndDebuggerAddPlayingEvents();
	}

	if(g_audio_state.editor_active_func)
	{
		editorActive = g_audio_state.editor_active_func();
	}

	if(editorActive!=lastEditorActive && g_audio_dbg.debugging)
	{
		Color blue = {0x00,0x00,0xFF,0xFF};
		ui_SliderAddSpecialValue(g_audio_dbg.setting_pane.debug_tick, gMaxFlowNodeTick-1, blue);
	}

	lastEditorActive = editorActive;

	ui_SliderSetRange(g_audio_dbg.setting_pane.debug_tick, 0, gMaxFlowNodeTick-1, 1);
}

void sndDebuggerRegister(void)
{
	g_audio_dbg.dbger = gDebugRegisterDebugger("Audio");
	gDebuggerSetInitCallback(g_audio_dbg.dbger, sndDebuggerInit);
	gDebuggerSetTickCallback(g_audio_dbg.dbger, sndDebuggerTick);
	gDebuggerSetSettingsTickCallback(g_audio_dbg.dbger, sndUpdateAudioSettingsArea);
	gDebuggerSetPaneCallbacks(g_audio_dbg.dbger, sndPopulateAudioSettingsArea, sndPopulateAudioInfoArea);
	gDebuggerSetUserPaneCallback(g_audio_dbg.dbger, sndPopulateAudioUserPane);
}

void sndDebuggerCleanRefsToDSP(void *dsp)
{
	if(g_audio_dbg.debugging && snd_flownode_stash && dsp)
	{
		stashAddressRemovePointer(snd_flownode_stash, dsp, NULL);
	}
}

void sndDebuggerHandleReload(void)
{
	Color red = {0xFF, 0x00, 0x00, 0xFF};
	if(!g_audio_dbg.debugging)
	{
		return;
	}

	ui_SliderAddSpecialValue(g_audio_dbg.setting_pane.debug_tick, gMaxFlowNodeTick-1, red);
}

void sndUpdateDebugging(void)
{
	if(smdu.memory_area && smdu.memory_tree)
	{
		F32 h = ui_WidgetTreeGetHeight(smdu.memory_tree);
		ui_ScrollAreaSetSize(smdu.memory_area, 10000, h);
	}
}

typedef struct MemEntryNode MemEntryNode;

struct MemEntryNode {
	int count;
	int size;
	int total_size;
	int origin;
	const char *name;
	MemEntryNode *parent;
	MemEntryNode **children;
};

MemEntryNode **nodeEntries;
MemEntryNode **parentEntries;
StashTable entryStash;

void sndDebugMemoryTreeAddSize(MemEntryNode *node, int size)
{
	MemEntryNode *curnode = node;

	while(curnode)
	{
		curnode->total_size += size;

		curnode = curnode->parent;
	}
}

int sndDebugMemoryBuildTree(void *tracker, int origin, void *parent, void *object, const char *name)
{
	MemEntryNode *node = NULL;
	MemEntryNode *parent_node = NULL;
	int size = 0;

	stashAddressFindPointer(entryStash, parent, &parent_node);

	if(parent_node)
	{
		int i;
		for(i=0; i<eaSize(&parent_node->children); i++)
		{
			node = parent_node->children[i];

			if(name && node->name && !stricmp(node->name, name))
			{
				sndDebugMemoryTreeAddSize(node, size);
				node->size += size;
				node->count++;

				stashAddressAddPointer(entryStash, object, node, 1);

				return 1;
			}
		}
	}
	else
		printf("");			// Debugging purposes

	node = calloc(1, sizeof(MemEntryNode));

	node->name = name;
	node->origin = origin;
	node->size = size;
	node->count = 1;

	eaPush(&nodeEntries, node);
	stashAddressAddPointer(entryStash, object, node, 1);

	if(parent && parent_node)
	{
		node->parent = parent_node;
		eaPush(&parent_node->children, node);

		sndDebugMemoryTreeAddSize(node, size);
	}
	else
	{
		eaPush(&parentEntries, node);
	}
	return 1;
}

int sndDebugMemoryCmp(const MemEntryNode **right, const MemEntryNode **left)
{
	return (*left)->total_size - (*right)->total_size;
}

//typedef void (*UITreeChildrenFunc)(UITreeNode *parent, UserData fillData);
void sndDebugMemoryUIFill(UIWidgetTreeNode *parent, UserData fillData)
{
	MemEntryNode ***children = NULL;
	int i;
	if(!fillData)
	{
		// root
		children = &parentEntries;
	}
	else
	{
		MemEntryNode *node = (MemEntryNode*)fillData;
		children = &node->children;
	}

	eaQSort(*children, sndDebugMemoryCmp);

	for(i=0; i<eaSize(children); i++)
	{
		MemEntryNode *child = (*children)[i];
		char str[MAX_PATH];
		UIWidgetTreeNode *treenode = NULL;

		//if(child->name) 
		//	sprintf(str, "%s:%s - %d/%d", fmodMemoryTrackerGetOriginStr(child->origin), child->name, child->size, child->total_size);
		//else 
		//	sprintf(str, "%s - %d/%d", fmodMemoryTrackerGetOriginStr(child->origin), child->size, child->total_size);
		
		treenode = ui_WidgetTreeNodeCreate(smdu.memory_tree, str, 0, NULL, child, sndDebugMemoryUIFill, child, NULL, NULL, 15);
		ui_WidgetTreeNodeAddChild(parent, treenode);
	}
}

void freeMemEntryNode(MemEntryNode *ptr)
{
	eaDestroy(&ptr->children);
	free(ptr);
}

bool sndDebugMemoryWindowClose(UIAnyWidget *widget, UserData data)
{
	stashTableClear(entryStash);
	eaClearEx(&nodeEntries, freeMemEntryNode);
	eaClear(&parentEntries);

	smdu.memory_window = NULL;

	GamePrefStoreFloat("SMDU.x", ui_WidgetGetX(widget));
	GamePrefStoreFloat("SMDU.y", ui_WidgetGetY(widget));
	GamePrefStoreFloat("SMDU.w", ui_WidgetGetWidth(widget));
	GamePrefStoreFloat("SMDU.h", ui_WidgetGetHeight(widget));

	return 1;
}

#endif

AUTO_COMMAND;
void sndDebugMemoryUI(void)
{
#ifndef STUB_SOUNDLIB
	if(!entryStash)
		entryStash = stashTableCreateAddress(10);

	if(smdu.memory_window)
		ui_WindowClose(smdu.memory_window);
	else
	{
		F32 smx, smy, smw, smh;
		//fmodDumpMemory(sndDebugMemoryBuildTree);
		if(smdu.memory_window)
		{
			ui_WindowHide(smdu.memory_window);
			ui_WidgetQueueFreeAndNull(&smdu.memory_window);
		}
		smx = GamePrefGetFloat("SDMU.x", 0);
		smy = GamePrefGetFloat("SDMU.y", 0);
		smw = GamePrefGetFloat("SDMU.w", 300);
		smh = GamePrefGetFloat("SDMU.h", 500);
		smdu.memory_window = ui_WindowCreate("SoundMemory", smx, smy, smw, smh);

		if(smdu.memory_window)
		{	
			smdu.memory_area = ui_ScrollAreaCreate(0, 0, 0, 0, 10000, 10000, 1, 1);
			ui_WidgetSetDimensionsEx(UI_WIDGET(smdu.memory_area), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
			ui_WindowAddChild(smdu.memory_window, smdu.memory_area);

			smdu.memory_tree = ui_WidgetTreeCreate(0, 0, 0, 0);
			ui_WidgetSetDimensionsEx(UI_WIDGET(smdu.memory_tree), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
			ui_WidgetTreeNodeSetFillCallback(smdu.memory_tree->root, sndDebugMemoryUIFill, NULL);

			ui_WidgetTreeNodeExpandAndCallback(smdu.memory_tree->root);

			ui_ScrollAreaAddChild(smdu.memory_area, UI_WIDGET(smdu.memory_tree));
		}

		ui_WindowSetCloseCallback(smdu.memory_window, sndDebugMemoryWindowClose, NULL);
		ui_WindowShow(smdu.memory_window);
	}
#endif
}

AUTO_COMMAND;
void sndDebugIncFlowNode(void)
{
#ifndef STUB_SOUNDLIB
	gFlowNodeTick++;
#endif
}

AUTO_COMMAND;
void sndDebugDecFlowNode(void)
{
#ifndef STUB_SOUNDLIB
	gFlowNodeTick--;
#endif
}

//#ifndef STUB_SOUNDLIB
#include "sndDebug2_c_ast.c"
//#endif
