/***************************************************************************



***************************************************************************/

#pragma once

#ifndef _SNDDEBUG2_H
#define _SNDDEBUG2_H
GCC_SYSTEM

// The goal here is to divorce the debug system from the actual systems as much as feasible
// The reasoning is that a broken system shouldn't affect the debugging

#include "stdtypes.h"

#include "sndLibPrivate.h"

typedef struct UITreeNode UITreeNode;
typedef struct UIWindow UIWindow;
typedef struct UIButton UIButton;
typedef struct UITree UITree;
typedef struct UICheckButton UICheckButton;
typedef struct UISlider UISlider;
typedef struct UITreeNode UITreeNode;
typedef struct UITabGroup UITabGroup;
typedef struct UIList UIList;
typedef struct UITab UITab;
typedef struct UILabel UILabel;
typedef struct UIPane UIPane;
typedef struct UITextArea UITextArea;
typedef struct UIProgressBar UIProgressBar;
typedef struct GlobalParam GlobalParam;
typedef struct Model Model;
typedef struct DynParticle DynParticle;
typedef struct UICoordinate UICoordinate;
typedef struct UISkin UISkin;
typedef struct UIBarGraph UIBarGraph;

typedef struct SoundSource SoundSource;
typedef struct SoundSourceDebugPersistInfo SoundSourceDebugPersistInfo;

typedef struct SoundDebugState {
	Debugger *dbger;
	REF_TO(DebuggerObject) events;
	REF_TO(DebuggerObject) spaces;
	//DebuggerObject *dsptree;
	DebuggerType project_type;
	DebuggerType group_type;
	DebuggerType event_type;
	DebuggerType instance_type;
	DebuggerType ss_type;
	DebuggerType ssc_type;
	DebuggerType ssct_type;
	DebuggerType dsp_type;

	DebuggerFlag event_playing;
	DebuggerFlag event_played;
	DebuggerFlag space_active;
	DebuggerFlag volume_setting;

	struct {
		UIList *info_list;
		UILabel **connection_levels;
	} info_pane;

	struct {
		UIButton *reload;
		UILabel *memory_label;
		UIProgressBar *memory;
		UILabel *memory_info_label;
		UILabel *cpu_label;
		UILabel *cpu_value_label;
		UIProgressBar *cpu;
		UICheckButton *debug_dsp;
		UISlider *debug_tick;

		UILabel *current_space_label;
		UICheckButton *bypass_dsp;
		UILabel *current_space;
		UILabel *last_music_label;
		UITextArea *last_music;
		
		F32 max_cpu_value;
	} setting_pane;

	UISkin *vol_set_skin;
	// For instance_type
	SoundSource *debugged_source;
	SoundSourceDebugPersistInfo *debugged_persist;
	// For event_type
	void *debugged_info;

	DSPFlowNode	*headflownode;
	U32 debugging : 1;
	U32 reload : 1;
	U32 debug_DSP_flow : 1;
} SoundDebugState;

extern SoundDebugState g_audio_dbg;

void sndDebuggerRebuildEventTree(void);

void sndDebugAddDSPEvent(int argb);

void sndDebuggerRegister(void);

void sndDebuggerCleanRefsToDSP(void *dsp);
void sndDebuggerHandleReload(void);
void sndUpdateDebugging(void);

void sndDebugSetCallbacks(void);

#endif
