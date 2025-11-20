#pragma once
GCC_SYSTEM

/***************************************************************************



***************************************************************************/

/* This file contains the public interface to the sound library */


#ifndef _SNDOBJECT_H
#define _SNDOBJECT_H

#include "stdtypes.h"

typedef struct SoundObject SoundObject;
typedef struct SoundFadeManager SoundFadeManager;
typedef struct DSPFlowNode DSPFlowNode;

typedef enum {
	SNDOBJMSG_GET_NAME,
	SNDOBJMSG_INIT,
	SNDOBJMSG_CLEAN,
	SNDOBJMSG_MEMTRACKER,
	SNDOBJMSG_DESTROY,
} SoundObjectMsgType;

typedef struct SoundObjectMsg {
	SoundObjectMsgType type;

	union {
		struct {
			char *str;
			int len;
		} get_name;
		struct {
			void *tracker;
		} memtracker;
	} out;

	struct {
		struct {
			U32 reload : 1;
		} clean;
	} in;
} SoundObjectMsg;

typedef U32 (*SoundObjectMsgHandler)(SoundObject *obj, SoundObjectMsg *msg);

typedef struct SoundObjectClass {
	int id;
	const char *name;

	SoundObjectMsgHandler msgHandler;
	U32 count;
} SoundObjectClass;

typedef struct SoundObject {
	SoundObjectClass *soc;
	const char *file_name;	// POOLED
	const char *desc_name;  // POOLED
	const char *orig_name;  // POOLED or STATIC
	void *original_conn;			// DSP conn created by FMOD by default
	void *fmod_channel_group;		// DSP unit with all SFX applied
	void *fmod_dsp_base;			// DSP unit bypassing SFX
	void *fmod_dsp_unit;

	// For debugging
	DSPFlowNode *flownode_base;
	DSPFlowNode *flownode_unit;
	DSPFlowNode *flownode_chan;

	F32			debug_volume;
	U32			debug_volume_set;

	F32 effective_volume;
	F32 desired_volume;

	int mem_total;

	U32 has_dsp			: 1;
	U32 source_only		: 1;		// Cannot be a receiver, i.e. events
	U32 skip_dsp		: 1;		// Doesn't connect to DSPs?
} SoundObject;

void sndObjectRegisterClass(const char *name, SoundObjectMsgHandler handler);
void sndObjectCreateByName(SoundObject *obj, const char *class_name, const char *file_name, const char *desc_name, const char *orig_name);
void sndObjectDestroy(SoundObject *obj);

void sndObjectGetName(SoundObject *obj, char *str, int len);
void sndObjectSendInitMessage(SoundObject *obj);
void sndObjectSendDestroyMsg(SoundObject *obj);
void* sndObjectGetMemTracker(SoundObject *obj);

S32 sndFindOriginalConn(SoundObject *obj);

F32 sndObjectCalculateEffectiveVolume(SoundObject *obj);

#endif

