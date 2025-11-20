/***************************************************************************



***************************************************************************/

/* This file contains the public interface to the sound library */


#pragma once

#ifndef _SNDSPACE_H
#define _SNDSPACE_H
GCC_SYSTEM

#include "stdtypes.h"
#include "sndObject.h"

typedef struct SoundSpaceConnector SoundSpaceConnector;
typedef struct SoundObjectMsg SoundObjectMsg;
typedef struct DebuggerObject DebuggerObject;
typedef struct SoundMixerChannel SoundMixerChannel;
typedef struct Room Room;

typedef struct SoundSpaceVolume {
	Vec3 world_mid;
	Vec3 world_min;
	Vec3 world_max;
} SoundSpaceVolume;

typedef struct SoundSpaceSphere {
	Vec3 mid;
	F32 radius;
	int order;				// For sphere lines
} SoundSpaceSphere;

typedef enum SoundSpaceType {
	SST_VOLUME,
	SST_SPHERE,
	SST_NULL,
} SoundSpaceType;

typedef struct CachedConnector
{
	SoundSpaceConnector *conn;
	F32 cost;
} CachedConnector;

typedef struct SoundSpaceConnectorCache {
	CachedConnector **connList;
	bool dirty;
} SoundSpaceConnectorCache;

typedef struct SoundSpace {
	SoundObject obj;
	int priority;
	int multiplier;
	
	SoundSource **ownedSources;			// Sources existing because of the space, e.g room tones
	SoundSource **localSources;	

	const char *music_name; // the name of the Music Event (if the space has music)

	REF_TO(DebuggerObject) debug_object;
	
	union {
		SoundSpaceVolume volume;
		SoundSpaceSphere sphere;
	};

	SoundSpaceType type;

	REF_TO(SoundDSP) dsp_ref;

	SoundSpaceConnector **connectors;		// Connections with other spaces - conn->target==me
	SoundSpaceConnectorCache cache;

	U32 id;									// For connectors to know to whom they're attached

	SoundMixerChannel *soundMixerChannel;
	SoundSource *pLineSoundSource;		// keep track of the sound source for the line

	U32				is_audible	: 1;
	U32				is_current	: 1;
	U32				is_nearby	: 1;
	U32				is_dsp		: 1;		// Tells me if I have a non-mixer DSP, e.g. cave
	U32				destroyed	: 1;
	U32				non_exclude : 1;
	U32				was_audible	: 1;		// Checked to determine if channelgroup should be destroyed
} SoundSpace;

int sndSrcCmp(const SoundSource **s1, const SoundSource **s2);

void sndUpdateSpaces(void);
void sndSpacesCleanup(void);

void sndSpaceCreateAndRegisterNullSpace();
SoundSpace* sndSpaceCreateFromRoom(Room *room);

// registers the space with the system
void sndSpaceRegister(SoundSpace *space);

U32 sndSpaceObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg);
void sndSpaceInit(SoundSpace *space, const char* dsp_str, const char* filename, const char* group_name);
void sndSpaceDestroy(SoundSpace *space);
void sndSpaceFree(SoundSpace *space);
SoundSpace *sndSpaceGetByName(const char *name);

void sndSphereDestroy(SoundSpace *sphere);
SoundSpace *sndSphereCreate(const char *event_name, const char *excluder_str, const char *dsp_str, 
							const char *editor_group_str, const char *sound_group_str, const char *sound_group_ord, 
							const Mat4 world_mat);

void sndSpaceConnectConnector(SoundSpace *space1, SoundSpace *space2, SoundSpaceConnector *conn);
void sndSpaceGetCenter(const SoundSpace *space, Vec3 posOut);
SoundSpaceConnector *sndSpacesGetConn(SoundSpace *space1, SoundSpace *space2);

SoundSpace* sndSpaceFindCurrent(void);
SoundSpace* sndSpaceFind(const Vec3 pos);

void sndSpaceProcessSource(SoundSpace *space, SoundSource *source);

typedef int (*sndSpaceTraverseFunc)(SoundSpace *space, SoundSpaceConnector *conn, void* userdata);
void sndSpaceTraverse(SoundSpace *start, U32 self, U32 audible_only, sndSpaceTraverseFunc func, void* userdata);

int sndSpaceCalcAudibility(SoundSpace *space, SoundSpaceConnector *conn, void *unused);

void sndUpdateSourceGroup(SoundSourceGroup *group, int space_update);

int sndSpaceAddChannelGroup(SoundSpace *space, void *cg);

const char* sndSpaceName(SoundSpace *space);

void sndSourceGroupGrantEvent(SoundSourceGroup *group, SoundSource *source);



void sndSpaceAddConnector(SoundSpace *soundSpace, SoundSpaceConnector *soundSpaceConnector);

// create an event reverb for the space
//void sndSpaceCreateEventReverb(SoundSpace *soundSpace);

#endif

