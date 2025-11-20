#ifndef _SNDSERVER_H_
#define _SNDSERVER_H_

#include "stdtypes.h"

typedef struct GameAudioEventMap GameAudioEventMap;
typedef struct AudioStatusWatch AudioStatusWatch;

// Public interface to sound lib on the server... very restricted and unique (i.e. not client) functionality
void sndServerInit(void);
void sndLibServerOncePerFrame(F32 elapsed);
void sndServerMapLoad(ZoneMap *map);
void sndServerMapUnload(void);

// Used to start and stop tracking a map when reload is done
void sndGAEMapStartTracking(GameAudioEventMap *map);
void sndGAEMapStopTracking(GameAudioEventMap *map);

void sndServerMapTransfer(Entity *e);

typedef struct SoundServerPartitionState {
	AudioStatusWatch **watches;
} SoundServerPartitionState;

AUTO_STRUCT;
typedef struct SoundServerState {
	U32 needsInitEnc : 1;

	S64 nextAdvertPass;
	U32 advertTimeout;					AST(DEFAULT(15*60))
	U32 advertMapTransitionTimeout;		AST(DEFAULT(5*60))

	SoundServerPartitionState **partitions;		NO_AST
} SoundServerState;

extern SoundServerState snd_server_state;

#endif