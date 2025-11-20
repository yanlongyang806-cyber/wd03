/***************************************************************************



***************************************************************************/

/* This file contains the public interface to the sound library */

#pragma once

#ifndef _SNDSOURCE_H
#define _SNDSOURCE_H
GCC_SYSTEM

#include "stdtypes.h"
#include "sndObject.h"
#include "referencesystem.h"

typedef U32 EntityRef;
typedef struct SoundSourceGroup SoundSourceGroup;
typedef struct EventMetaData EventMetaData;
typedef struct SoundObjectMsg SoundObjectMsg;
typedef struct Debugger Debugger;
typedef struct DebuggerRoot DebuggerRoot;
typedef struct DebuggerObject DebuggerObject;
typedef int DebuggerType;
typedef struct SoundSpace SoundSpace;
typedef struct SoundSpaceConnector SoundSpaceConnector;
typedef struct GlobalParam GlobalParam;
typedef struct SoundSourceDebugInfo SoundSourceDebugInfo;
typedef struct SoundSourceCluster SoundSourceCluster;
typedef struct SoundMixerChannel SoundMixerChannel;

typedef enum SoundEventType
{
	SE_EFFECT,
	SE_SPHERE,
	SE_VOLUME,
	SE_REMOTE,
	SE_MUSIC,
} SoundEventType;

typedef struct MusicSource {
	EntityRef entRef;
} MusicSource;

typedef struct PSEffect {
	int guid;
} PSEffect;

typedef struct PSSphere {
	F32 radius;
} PSSphere;

typedef struct PointSource {
	Vec3 pos;
	Vec3 vel;
	Vec3 last_vel;
	Vec3 dir;

	EntityRef entRef;

	union {
		PSEffect effect;
		PSSphere sphere;
	};
} PointSource;

typedef struct RoomSource {
	SoundSpace *space;
} RoomSource;

typedef struct UISource {
	int unused;
} UISource;

typedef enum SourceType {
	ST_MUSIC,
	ST_POINT,
	ST_ROOM,
	ST_UI,
} SourceType;

typedef struct SourceLine {
	const char *group_str;
	SoundSpace **spaces;
} SourceLine;

typedef enum SourceOrigin {
	SO_FX,
	SO_WORLD,
	SO_REMOTE,
	SO_UI,
} SourceOrigin;

AUTO_STRUCT;
typedef struct SoundSourceDebugPathLeg {
	Vec3 collision;

	Vec3 line_start;
	Vec3 line_end;

	Vec3 conn_local_min;
	Vec3 conn_local_max;
	Mat4 conn_mat;
} SoundSourceDebugPathLeg;

AUTO_STRUCT;
typedef struct SoundSourceDebugInfo {
	SoundSourceDebugPathLeg **pathlegs;
} SoundSourceDebugInfo;

typedef struct SoundSource {
	SoundObject obj;
	void *fmod_event;
	void *info_event;

	union {
		MusicSource music;					// 2D source, originating from music state machine
		PointSource point;					// 3D source, originating from geometry, FX, AI, etc.
		RoomSource room;					// 2D source, originating from a volume/soundspace
		UISource ui;						// 2D source, originating from user interface stuff, floaters, etc.
	};

	SourceType type;
	SourceOrigin orig;
	SourceLine line;						// 3D source, spread along a line from spheres

	Vec3		origin_pos;					// Location, pre transmission
	Vec3		origin_dir;					// Location, post transmission
	Vec3		virtual_pos;				// Where this sound is, after transmission
	Vec3		virtual_dir;				// Where it faces, after transmission
	Vec3		last_virtual_pos;			// Where this sound was last frame, for speed limiting
	Vec3		relative_pos;				// Position relative to listener at the onset of the event
	S64			last_virtual_time;			// Last time this was virtualized
	F32			distToListener;				// How far, virtually, the sound is to the listener
	F32			directionality;				// How much this event is panned by 3D pos
	F32			pseudoAttenuate;			// This allows 2D events to fade over "distance"
	F32			volume;						// current volume

	SoundSpace  *ownerSpace;					// Which SndSpc made this source if any
	SoundSpace	*originSpace;				// Where this sound started off in life - can change with mobile sounds
	SoundSpaceConnector *connToListener;	// The this sound traveled to get to me

	F32			fade_level;
	F32			time_stored;				// When the sound was first created - not necessarily when first played
	F32			length;

	F32 currentAmp; // current amplitude of signal

	GlobalParam **gps;
	SoundSourceGroup *group;
	EventMetaData *emd;
	REF_TO(DebuggerObject) debug_object;

	int memory_usage;
	int memory_usage_max;
	SoundSourceDebugInfo source_info;

	SoundSourceCluster *cluster;
	SoundMixerChannel *soundMixerChannel;

	void *ampAnalysisDSP; // pointer to FMOD DSP for doing amp analysis

	U32			in_audible_space		: 1;	// Is this sound in a space that is audible
	U32			is_audible				: 1;	// Is this sound within radius+delta (i.e. could be heard soon)
	U32			is_muted				: 1;	// Have I been muted (left an audible space, space became inaudible)
	U32			is_virtual				: 1;	// Is this sound outside of its origin space
	U32			destroyed				: 1;	//
	U32			clean_up				: 1;	// Tells looping sounds to die
	U32			immediate				: 1;	// When stopped, don't let it fade out
	U32			moved					: 1;
	U32			unmuted					: 1;	// FMOD sometimes keeps muted state for some reason
	U32			needs_start				: 1;
	U32			needs_start_offset		: 1;
	U32			needs_move				: 1;
	U32			needs_channelgroup		: 1;
	U32			needs_stop				: 1;
	U32			has_event				: 1;
	U32			started					: 1;
	U32			dead					: 1;
	U32			stopped					: 1;
	U32			stolen					: 1;
	U32			hidden					: 1;
	U32			inst_deleted			: 1;
	U32			lod_muted				: 1; // when the LOD system has 'stopped' the event, this flag is set
	U32			updatePosFromEnt		: 1; // update 3d position based on the entity's position
	U32			bLocalPlayer			: 1;
	U32			stationary				: 1; // if true, the source will not move from its initial position (e.g. gun shot sound fx)
	U32			enqueued				: 1;
	U32			patching				: 1; // waiting for the file it needs to be patched before starting
	U32			drivesContactDialogAnim	: 1;
	U32			runtimeError			: 1;
	U32			onStreamedList			: 1;
} SoundSource;

AUTO_STRUCT;
typedef struct SoundSourceDebugPersistInfo {
	Vec3 pos;
	Vec3 vel;
	Vec3 dir;
	F32 directionality;
	F32 dist;
	F32 radius;
	int type;
	int memory_usage_max;
	void *info_event;  NO_AST
	//F32 **path;
} SoundSourceDebugPersistInfo;

typedef struct SoundSourceGroup {
	const char *name;
	int fmod_id;
	void *fmod_info_event;
	EventMetaData *emd;
	SoundSource **inactive_sources;
	SoundSource **active_sources;
	SoundSource **dead_sources;

	REF_TO(DebuggerObject) inactive_object;
	REF_TO(DebuggerObject) active_object;
	REF_TO(DebuggerObject) dead_object;
} SoundSourceGroup;



// Creation & Destruction Callbacks 
//
typedef void (*SoundSourceCreatedFunc)(SoundSource *source, void *userData);
typedef void (*SoundSourceDestroyedFunc)(SoundSource *source, void *userData);

typedef struct SoundSourceCreatedCB {
	SoundSourceCreatedFunc soundSourceCreatedFunc;
	void *userData;
} SoundSourceCreatedCB;

typedef struct SoundSourceDestroyedCB {
	SoundSourceDestroyedFunc soundSourceDestroyedFunc;
	void *userData;
} SoundSourceDestroyedCB;

void sndSourceSetCreatedCB(SoundSourceCreatedFunc func, void* userData);
void sndSourceSetDestroyedCB(SoundSourceDestroyedFunc func, void* userData);

void sndSourceClearGroups();
void sndSourceClearGroup(SoundSource *source);

//
//
SoundSource *sndSourceCreate(const char *file_name, const char *orig_name, const char *event_name, const Vec3 pos, SourceType type, SourceOrigin orig, SoundSpace *owner, U32 entRef, bool failQuietly);
U32 sndSourceObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg);
void sndSourceDestroy(SoundSource *source);
void sndSourceFree(SoundSource *source);

void sndSourceGroupDestroy(SoundSourceGroup *group);

void sndSourcePreprocess(SoundSource *source);
void sndSourceChangeSpaces(SoundSource* source, SoundSpace *new_space);
void sndSourceCheckAudibility(SoundSource* source);
F32 sndSourceDistToListener(SoundSource *source, SoundSpaceConnector *conn, F32 *directionality, Vec3 final_pos, Vec3 final_dir);
int sndSourcePlay(SoundSource *source);
void sndSourceRemoveEvent(SoundSource *source);

F32 sndSourcePathToPosition(SoundSource *source, const Vec3 target, SoundSpaceConnector *conn, SoundSpaceConnector ***conns, F32 ***points);
F32 sndSourceConnDist(int index, SoundSpaceConnector ***conns, F32 ***points, F32 radius, F32 *directionality);
void sndSourceSetConnToListener(SoundSource *source, SoundSpaceConnector *conn);

void sndSourceGetOrigin(const SoundSource *source, Vec3 posOut);
U32 sndSourceIsVisible(SoundSource *source);
void sndSourceAddPoint(SoundSource *source, SoundSpace *space);
void sndSourceAddGroup(SoundSource *source, const char* group);
void sndSourceDelPoint(SoundSource *source, SoundSpace *space);
int sndSourceFindGroup(const char *group, SoundSource **source);
void sndSourceGroupDelInstance(SoundSourceGroup *group, SoundSource *source);
void sndSourceGroupAddInstance(SoundSourceGroup *group, SoundSource *source);

FMOD_RESULT F_CALLBACK sndSourceStolenCB(void *event, int type, void* p1, void *p2, void* data);
FMOD_RESULT F_CALLBACK sndSourceStopCB(void *event, int type, void* p1, void *p2, void* data);
FMOD_RESULT F_CALLBACK sndSourceStartCB(void *event, int type, void* p1, void *p2, void* data);

void sndSourceMove(SoundSource *source, Vec3 new_pos, Vec3 new_vel, Vec3 new_dir);

void sndSourceValidatePlaying(void);

// Accessors
void sndSourceGetFile(void *event_data, char *buffer, int len);
F32 sndSourceUpdatePosition(SoundSource *source);

#endif

