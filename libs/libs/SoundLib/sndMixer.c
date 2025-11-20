#ifndef STUB_SOUNDLIB

#include "sndMixer.h"

// Cryptic Structs
#include "earray.h"
#include "estring.h"
#include "mathutil.h"
#include "timing.h"
#include "MemoryPool.h"

// Sound structs
#include "sndLibPrivate.h"
#include "event_sys.h"
#include "sndFade.h"
#include "sndSource.h"
#include "sndSpace.h"
#include "sndConn.h"

#include "sndLibPrivate_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

#define AUDIBLE_DISTANCE (60.0) // ft
#define NUM_VOICES (40) // the number of simultaneous FMOD events allowed before one-shot events will begin to be stolen (oldest first)

typedef struct FmodEventTracker {
	void *fmodEvent;
	U32 timestamp;
	U32 canSteal : 1;
	U32 stolen : 1;
};

MP_DEFINE(FmodEventTracker);


///////////////////////////////////////////////////////////////////////////////
// SoundMixerChannel Functions
///////////////////////////////////////////////////////////////////////////////

// private methods
void sndMixerChannelCreateFmodChannelGroup(SoundMixerChannel *soundMixerChannel);

// remove all sources from channel
void sndMixerChannelRemoveAllSources(SoundMixerChannel *soundMixerChannel);

// remove source (option to remove from stash)
void sndMixerChannelRemoveSourceEx(SoundMixerChannel *soundMixerChannel, SoundSource *source, bool removeFromStash);

// remove channel group if any
void sndMixerChannelRemoveFmodChannelGroup(SoundMixerChannel *soundMixerChannel);


SoundMixerChannel *sndMixerChannelCreate(SoundSpace *soundSpace)
{
	SoundMixerChannel *soundMixerChannel = calloc(1, sizeof(SoundMixerChannel));
	sndMixerChannelInit(soundMixerChannel, soundSpace);
	return soundMixerChannel;
}

void sndMixerChannelInit(SoundMixerChannel *soundMixerChannel, SoundSpace *soundSpace)
{
	soundMixerChannel->soundSpace = soundSpace;
	soundMixerChannel->sendToReverb = true;
}

void sndMixerChannelRemoveFmodChannelGroup(SoundMixerChannel *soundMixerChannel)
{
	sndMixerChannelRemoveAllSources(soundMixerChannel);
}

//! destroy a mixer channel
void sndMixerChannelDestroy(SoundMixerChannel *soundMixerChannel)
{
	if(!soundMixerChannel) return;
	
	// remove all sources
	sndMixerChannelRemoveAllSources(soundMixerChannel);

	// free the struct
	free(soundMixerChannel);
}

void sndMixerChannelRemoveAllSources(SoundMixerChannel *soundMixerChannel)
{
	StashTableIterator iter;
	StashElement elem;
	SoundSource *source;

	stashGetIterator(soundMixerChannel->stSources, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		source = (SoundSource*)stashElementGetKey(elem);
		sndMixerChannelRemoveSourceEx(soundMixerChannel, source, false);
	}

	stashTableClear(soundMixerChannel->stSources);
}

void sndMixerChannelSetMute(SoundMixerChannel *soundMixerChannel, bool mute)
{
	soundMixerChannel->isMuted = mute;
}

bool sndMixerChannelIsMuted(SoundMixerChannel *soundMixerChannel)
{
	return soundMixerChannel->isMuted;
}

void sndMixerChannelAddSource(SoundMixerChannel *soundMixerChannel, SoundSource *source)
{
	if (!soundMixerChannel->stSources) {
		soundMixerChannel->stSources = stashTableCreateAddress(16);
	}
	stashAddPointer(soundMixerChannel->stSources, source, NULL, false);
	assert(!source->soundMixerChannel);
	source->soundMixerChannel = soundMixerChannel;
}

void sndMixerChannelConnectSource(SoundMixerChannel *soundMixerChannel, SoundSource *source)
{
	// devassert(source->fmod_event); called functions check for event

	void *soundSourceCG = fmodEventGetChannelGroup(source->fmod_event);
	static void **dsps = NULL;
	static void **connections = NULL;
	void *dspHead = fmodChannelGroupGetDSPHead(soundSourceCG);
	int numConnections, i;

	eaClear(&dsps);
	eaClear(&connections);

	// The Fmod Event has just been connected to the DSP graph
	// Fmod creates a default connection - here we want to disconnect any defaults
	fmodDSPGetOutputs(dspHead, &dsps, &connections);

	numConnections = eaSize(&connections);
	for(i = 0; i < numConnections; i++)
	{
		fmodDSPConnectionDisconnect(connections[i]);
	}

	// Connect the actual Fmod event to our mixer
	if(soundMixerChannel->sendToReverb)
		fmodChannelGroupAddGroup(soundMixerChannel->soundMixer->fmodWetCG, soundSourceCG);
	else
		fmodChannelGroupAddGroup(soundMixerChannel->soundMixer->fmodDryCG, soundSourceCG);
}

SoundSpace* sndMixerChannelSoundSpace(SoundMixerChannel *soundMixerChannel)
{
	return soundMixerChannel->soundSpace;
}

void sndMixerChannelRemoveSource(SoundMixerChannel *soundMixerChannel, SoundSource *source)
{
	sndMixerChannelRemoveSourceEx(soundMixerChannel, source, true);
}

void sndMixerChannelRemoveSourceEx(SoundMixerChannel *soundMixerChannel, SoundSource *source, bool removeFromStash)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if (source->ampAnalysisDSP) {
		fmodDSPFree(&source->ampAnalysisDSP, false);
	}

	if(stashFindInt(soundMixerChannel->stSources, source, NULL))
	{
		if(source->has_event)
		{
			// devassert(source->fmod_event); called functions check for event
			void* sourceCG = fmodEventGetChannelGroup(source->fmod_event);
			if(sourceCG)
				fmodChannelGroupDisconnect(sourceCG);
		}

		if(removeFromStash)
			stashRemovePointer(soundMixerChannel->stSources, source, NULL);

		source->soundMixerChannel = NULL;
	}

	PERFINFO_AUTO_STOP();
}

///////////////////////////////////////////////////////////////////////////////
// SoundMixer
///////////////////////////////////////////////////////////////////////////////

// private functions

//! update current space, return true if it changes
bool sndMixerUpdateCurrentSpace(SoundMixer *soundMixer);

//! determine audible spaces from current
void sndMixerUpdateAudibleSpaces(SoundMixer *soundMixer);

//! update the sources (determine audibility, etc)
void sndMixerUpdateSource(SoundMixer *soundMixer, SoundSource *source);

//! update the sources (determine audibility, etc)
void sndMixerUpdateSources(SoundMixer *soundMixer);

//! update the group (voice-stealing, etc)
void sndMixerUpdateSourceGroup(SoundMixer *soundMixer, SoundSourceGroup *soundSourceGroup);

//! determines what ChannelGroup the sound source should belong to
void sndMixerSetSoundSourceSpace(SoundMixer *soundMixer, SoundSource *source, SoundSpace *soundSpace);

//! find connectors within a given range within a space
// returns true if any found
bool sndMixerHelperConnectorsWithinDistance(SoundSpace *soundSpace, Vec3 pos, F32 thresholdDistance, SoundSpaceConnector ***soundSpaceConnectors);

// add src parameters to dst params
void sndMixerHelperAddReverbProperties(DSP_SfxReverb *dst, DSP_SfxReverb *src);

// scale src by scale and set dst
void sndMixerHelperScaleReverbProperties(DSP_SfxReverb *dst, DSP_SfxReverb *src, F32 scale);

typedef void (*SndMixerTraverseHelper)(SoundSpace *soundSpace, F32 distanceTraveled, F32 maxDistance, void *userData);


SoundMixer *sndMixerCreate()
{
	SoundMixer *soundMixer = calloc(1, sizeof(SoundMixer));
	sndMixerInit(soundMixer);
	return soundMixer;
}

void sndMixerInit(SoundMixer *soundMixer)
{
	// create a reverb if one doesn't exist
	if(!soundMixer->fmodWetDSP)
	{
		// create a channel group for the reverb
		soundMixer->fmodWetCG = fmodChannelGroupCreate(NULL, "Wet CG");
		soundMixer->fmodDryCG = fmodChannelGroupCreate(NULL, "Dry CG");

		// create the reverb
		fmodDSPCreateWithType(FMOD_DSP_TYPE_SFXREVERB, &soundMixer->fmodWetDSP, NULL);

		// connect the reverb to the channel group
		fmodChannelGroupAddDSP(soundMixer->fmodWetCG, soundMixer->fmodWetDSP);
	}

	sndMixerSetMaxPlaybacks(soundMixer, NUM_VOICES); // default
	soundMixer->numIgnoredEvents = 0;

	sndMixerSetFxDuckingEnabled(soundMixer, false);
	sndMixerSetDuckNumEventThreshold(soundMixer, 12); // default to 12 events
	sndMixerSetDuckScaleTarget(soundMixer, 0.5); // 50% scale
	sndMixerSetFxDuckRate(soundMixer, 0.05); // 10 secs to get from 1.0 to 0.5
	
	soundMixer->numSkippedDueToMemory = 0;
}

void sndMixerRemoveAllSources(SoundMixer *soundMixer)
{
	int i;

	for(i = eaSize(&soundMixer->channels)-1; i >= 0; i--)
	{
		SoundMixerChannel *soundMixerChannel = soundMixer->channels[i];
		sndMixerChannelRemoveAllSources(soundMixerChannel);
	}
}

void* sndMixerReverbDSP(SoundMixer *soundMixer)
{
	return soundMixer->fmodWetDSP;
}

void sndMixerSetReverbReturnLevel(SoundMixer *soundMixer, F32 level)
{
	void *outputConnection = fmodDSPGetOutputConn(soundMixer->fmodWetDSP);
	if(outputConnection)
	{
		fmodDSPConnectionSetVolumeEx(outputConnection, level, __FILE__, __LINE__);

		// optimization - bypass reverb if we're not using it
		if(soundMixer->fmodWetDSP)
		{
			if(level <= 0.0 || !g_audio_state.dsp_enabled)
			{
				// bypass the reverb (to prevent processing)
				fmodDSPSetBypass(soundMixer->fmodWetDSP, true);
			}
			else
			{
				fmodDSPSetBypass(soundMixer->fmodWetDSP, false);
			}
		}
	}
}

F32 sndMixerReverbReturnLevel(SoundMixer *soundMixer)
{
	F32 level = 0.0;
	void *outputConnection = fmodDSPGetOutputConn(soundMixer->fmodWetDSP);
	if(outputConnection)
	{
		level = fmodDSPConnectionGetVolume(outputConnection);
	}
	return level;
}

void sndMixerAddChannel(SoundMixer *soundMixer, SoundMixerChannel *soundMixerChannel)
{
	soundMixerChannel->soundMixer = soundMixer; // mark the owner

	// add the space to our list to manage
	eaPush(&soundMixer->spaces, soundMixerChannel->soundSpace);

	eaPush(&soundMixer->channels, soundMixerChannel);
}

void sndMixerRemoveChannel(SoundMixer *soundMixer, SoundMixerChannel *soundMixerChannel)
{
	if(soundMixerChannel)
	{
		// remove the space from our list to manage
		eaFindAndRemove(&soundMixer->spaces, soundMixerChannel->soundSpace);

		eaFindAndRemove(&soundMixer->channels, soundMixerChannel);
	}
}

bool sndMixerUpdateCurrentSpace(SoundMixer *soundMixer)
{
	SoundSpace *lastSpace = soundMixer->currentSpace;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(!g_audio_state.player_exists_func) // No player... set to null
	{
		soundMixer->currentSpace = space_state.null_space;
	}
	else
	{
		Vec3 listenerPos;

		sndGetListenerSpacePos(listenerPos);
		soundMixer->currentSpace = sndSpaceFind(listenerPos);
	}

	PERFINFO_AUTO_STOP();

	return lastSpace != soundMixer->currentSpace; // did we change?
}

#define INFINITY (FLT_MAX)

//int gCount = 0;
//int nCount = 0;

void sndMixerHelperTraverseShortestPath(SoundSpace *previousSpace, SoundSpaceConnector *soundSpaceConnector, SoundSpace *targetSpace)
{
	int i, numEdges, numValidEdges;
	SoundSpace *currentSpace;
	SoundSpace *testTargetSpace;
	static SoundSpaceConnectorEdge **validEdges = NULL;

	//gCount++;

	testTargetSpace = sndSpaceConnectorOtherSpace(soundSpaceConnector, previousSpace);
	if(targetSpace == testTargetSpace)
	{
		// we're there
		soundSpaceConnector->visited = 1; // mark the node

		return;
	}

	// loop through all edges and
	// make sure the other connector DOES NOT connect to previous space

	numEdges = eaSize(&soundSpaceConnector->connectorEdges);

	eaClear(&validEdges);

	for(i = 0; i < numEdges; i++)
	{
		SoundSpaceConnectorEdge *soundSpaceConnectorEdge = soundSpaceConnector->connectorEdges[i];
		SoundSpaceConnector *otherConnector;

		//nCount++;

		otherConnector = sndSpaceConnectorEdgeOtherConnector(soundSpaceConnectorEdge, soundSpaceConnector);
		if(!sndSpaceConnectorConnectsToSpace(otherConnector, previousSpace))
		{
			if(!otherConnector->visited)
			{
				//F32 newCost = soundSpaceConnector->cost + soundSpaceConnectorEdge->cost;
				eaPush(&validEdges, soundSpaceConnectorEdge);
			}
		}
		
	}

	soundSpaceConnector->visited = 1; // mark the node

	eaQSort(validEdges, sndSpaceConnectorEdgesSortByCost);

	currentSpace = sndSpaceConnectorOtherSpace(soundSpaceConnector, previousSpace);
	
	numValidEdges = eaSize(&validEdges);
	for(i = 0; i < numValidEdges; i++)
	{
		SoundSpaceConnectorEdge *soundSpaceConnectorEdge = validEdges[i];
		SoundSpaceConnector *otherConnector;
		SoundSpace *nextSpace;
		F32 newCost;

		//nCount++;

		if(soundSpaceConnectorEdge)
		{
			otherConnector = sndSpaceConnectorEdgeOtherConnector(soundSpaceConnectorEdge, soundSpaceConnector);
			nextSpace = sndSpaceConnectorOtherSpace(otherConnector, currentSpace);

			newCost = soundSpaceConnector->cost + soundSpaceConnectorEdge->cost;
			if(newCost < otherConnector->cost || otherConnector->cost == INFINITY)
			{
				otherConnector->cost = newCost;
				otherConnector->previousConnector = soundSpaceConnector;
			}

			if(nextSpace != targetSpace)
			{
				sndMixerHelperTraverseShortestPath(currentSpace, otherConnector, targetSpace);
			}
		}
	}
}

bool sndMixerHelperConnectorsWithinDistance(SoundSpace *soundSpace, Vec3 pos, F32 thresholdDistance, SoundSpaceConnector ***soundSpaceConnectors)
{
	bool result = false;

	if(soundSpace)
	{
		int i, numConnectors;
		numConnectors = eaSize(&soundSpace->connectors);

		if(numConnectors > 0)
		{
			// find nearest connector in current space
			for(i = 0; i < numConnectors; i++)
			{
				F32 dist;
				Vec3 intersectionVec; 
				SoundSpaceConnector *soundSpaceConnector = soundSpace->connectors[i];

				//dist = distance3(soundSpaceConnector->world_mid, pos);
				dist = sndConnPointDist(soundSpaceConnector, pos, intersectionVec);

				soundSpaceConnector->cost = dist;

				if(dist <= thresholdDistance)
				{
					eaPush(soundSpaceConnectors, soundSpaceConnector);
					result = true;
				}
			}

			eaQSort(*soundSpaceConnectors, sndSpaceConnectorSortByCost);
		}
	}

	return result;
}

void sndMixerSetDistanceCacheNeedsUpdate(SoundMixer *soundMixer, bool val)
{
	int i;
	for(i=0; i<eaSize(&space_state.global_spaces); i++)
		space_state.global_spaces[i]->cache.dirty = true;
}

F32 sndMixerFindShortestDistanceToSpace(SoundMixer *soundMixer, SoundSpace *currentSpace, Vec3 currentPos, SoundSpace *targetSpace)
{
	int i, numConnectors = eaSize(&currentSpace->connectors);
	F32 distance = INFINITY;
	SoundSpaceConnectorCache *cache = &targetSpace->cache;

	if(cache->dirty || !cache->connList)
	{
		// reset visited flags
		for(i = 0; i < eaSize(&space_state.global_conns); i++)
		{
			SoundSpaceConnector *soundSpaceConnector = space_state.global_conns[i];
			soundSpaceConnector->visited = 0;
			soundSpaceConnector->cost = INFINITY;
		}

		// reset costs
		for(i = 0; i < numConnectors; i++)
		{
			currentSpace->connectors[i]->cost = 0;
		}

		for(i = 0; i < numConnectors; i++)
		{
			SoundSpaceConnector *soundSpaceConnector = currentSpace->connectors[i];
			sndMixerHelperTraverseShortestPath(currentSpace, soundSpaceConnector, targetSpace);
		}
		
		cache->dirty = false; // we've updated, so don't do it again

		// release & clear previous cache
		eaClearEx(&cache->connList, NULL);

		// Cache current connectors & their cost
 		for(i = 0; i < numConnectors; i++)
		{
			// cache the connector and cost
			CachedConnector *cacheConn = calloc(1, sizeof(CachedConnector));
			cacheConn->conn = currentSpace->connectors[i];
			cacheConn->cost = currentSpace->connectors[i]->cost;
			eaPush(&cache->connList, cacheConn);
		}
	}

	// set the cost from the cache
	for(i = eaSize(&cache->connList)-1; i >= 0; i--)
	{
		cache->connList[i]->conn->cost = cache->connList[i]->cost;
	}

	// Add the distances from our current position to the connectors
	// find nearest connector in current space
	for(i = 0; i < numConnectors; i++)
	{
		F32 dist;
		Vec3 intersectionVec; 
		SoundSpaceConnector *soundSpaceConnector = currentSpace->connectors[i];

		dist = sndConnPointDist(soundSpaceConnector, currentPos, intersectionVec);
		soundSpaceConnector->cost += dist;
	}

	// find the shortest distance
	eaQSort(targetSpace->connectors, sndSpaceConnectorSortByCost);

	if(eaSize(&targetSpace->connectors) > 0)
	{
		distance = targetSpace->connectors[0]->cost;
	}
	
	return distance;
}

void sndMixerUpdateChannelsAudibility(SoundMixer *soundMixer, F32 maxDistance)
{
	Vec3 listenerPos;
	int i, numSpaces;

	sndGetListenerSpacePos(listenerPos);
	
	// determine shortest distances to all spaces
	numSpaces = eaSize(&soundMixer->spaces);
	for(i = 0; i < numSpaces; i++)
	{
		SoundSpace *soundSpace = soundMixer->spaces[i];
		if(soundSpace != soundMixer->currentSpace)
		{
			F32 dist = sndMixerFindShortestDistanceToSpace(soundMixer, soundMixer->currentSpace, listenerPos, soundSpace);
			if(dist >= 0 && dist < maxDistance)
			{
				soundSpace->is_audible = 1;
				soundSpace->soundMixerChannel->audibility = CLAMP(1.0 - (dist / maxDistance), 0.0, 1.0);
			}
		}
		else
		{
			soundSpace->is_audible = 1;
			soundSpace->soundMixerChannel->audibility = 1.0;
		}
	}
}

void sndMixerUpdateAudibleSpaces(SoundMixer *soundMixer)
{
	int i, numSpaces, numChannels;
	SoundSpace *space = soundMixer->currentSpace;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	// clear all audible-flags
	numSpaces = eaSize(&soundMixer->spaces);
	for(i = 0; i < numSpaces; i++)
	{
		SoundMixerChannel *soundMixerChannel = soundMixer->spaces[i]->soundMixerChannel;

		soundMixer->spaces[i]->is_audible = 0;
		
		if(soundMixerChannel)
		{
			soundMixerChannel->audibility = 0.0;
		}
	}

	if(!space) 
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	// loop through all of the current space's connections, activating
	sndMixerUpdateChannelsAudibility(soundMixer, AUDIBLE_DISTANCE);

	numChannels = eaSize(&soundMixer->channels);

	// apply volume scales 
	for(i = 0; i < numChannels; i++)
	{
		SoundMixerChannel *soundMixerChannel = soundMixer->channels[i];
		
		sndMixerChannelSetMute(soundMixerChannel, false);// !soundMixerChannel->soundSpace->is_audible);
	}

	PERFINFO_AUTO_STOP();
}

void sndMixerSetSoundSourceSpace(SoundMixer *soundMixer, SoundSource *source, SoundSpace *soundSpace)
{
	// they're the same, bail
	if(soundSpace == source->originSpace) return;

	// Update Space Information ----
	
	// if changing, disconnect
	if(source->originSpace)
	{
		eaFindAndRemoveFast(&source->originSpace->localSources, source);
	}
	source->originSpace = soundSpace;
	// save the source in the local sources
	eaPush(&source->originSpace->localSources, source);
}

#define SND_AUDIBLE_RADIUS (20)

char **eaRuntimeAudioEventErrors = NULL;

void sndMixerUpdateSource(SoundMixer *soundMixer, SoundSource *source)
{
	SoundSpace *newOrigin = NULL;
	Vec3 pos;
	F32 distMoved = 0.0;
	bool shouldBeAudible;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	assert(!source->destroyed);

	if(!source->emd) 
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (source->fmod_event)
	{
		if (fmodEventHasRanPastEndWhileLoading(source->fmod_event))
		{
			// This should be a very rare occurrence ; Ideally, this never happens but we still need to handle it.
			// Also, I'm trying not to spam the errors since they'd probably get into the 10k+ range pretty fast during normal gameplay for a single person in one session
			FMOD_RESULT nresult;
			FMOD_RESULT sresult;
			char *name = NULL;

			nresult = fmodEventGetName(source->fmod_event, &name);
			if (!name) {
				name = "NULL";
			}

			if (g_audio_state.sound_developer &&
				eaFind(&eaRuntimeAudioEventErrors, name) < 0)
			{
				if (nresult != FMOD_OK) {
					ErrorDetailsf("Name Error: %s", fmodGetErrorText(nresult));
					// Don't add the NULL name on errors
				} else {
					ErrorDetailsf("Name: %s", name);
					eaPush(&eaRuntimeAudioEventErrors, name);
				}
				Errorf("Found one-shot audio event that ran past end while loading, possible FEV / FSB mismatch");
			}

			sresult = FMOD_EventSystem_StopEvent(source->fmod_event, true);
			source->runtimeError = 1;

			if (sresult != FMOD_OK) {
				if (nresult != FMOD_OK) {
					ErrorDetailsf("Name Error: %s", fmodGetErrorText(nresult));
				} else {
					ErrorDetailsf("Name: %s", name);
				}
				ErrorDetailsf("Stop Error: %s", fmodGetErrorText(sresult));
				Errorf("Error attempting to stop one-shot audio event that ran past end while loading");
			}

			PERFINFO_AUTO_STOP();
			return;
		}
		else if (fmodEventHasFevFsbMismatch(source->fmod_event))
		{
			// This should be a very rare occurrence ; Ideally, this never happens but we still need to handle it.
			// Also, I'm trying not to spam the errors since they'd probably get into the 10k+ range pretty fast during normal gameplay for a single person in one session
			FMOD_RESULT nresult;
			FMOD_RESULT sresult;
			char *name = NULL;

			nresult = fmodEventGetName(source->fmod_event, &name);
			if (!name) {
				name = "NULL";
			}

			if (eaFind(&eaRuntimeAudioEventErrors, name) < 0) {
				if (nresult != FMOD_OK) {
					ErrorDetailsf("Name Error: %s", fmodGetErrorText(nresult));
					// Don't add the NULL name on errors
				} else {
					ErrorDetailsf("Name: %s", name);
					eaPush(&eaRuntimeAudioEventErrors, name);
				}
				Errorf("Attempted to play audio event with mismatched FEV / FSB");
			}

			// only stop one-shots, let looping music & ambients continue as normal and be killed as normal
			if (!fmodEventIsLooping(source->fmod_event))
			{
				sresult = FMOD_EventSystem_StopEvent(source->fmod_event, true);
				source->runtimeError = 1;

				if (sresult != FMOD_OK) {
					if (nresult != FMOD_OK) {
						ErrorDetailsf("Name Error: %s", fmodGetErrorText(nresult));
					} else {
						ErrorDetailsf("Name: %s", name);
					}
					ErrorDetailsf("Stop Error: %s", fmodGetErrorText(sresult));
					Errorf("Error attempting to stop audio event with mismatched FEV / FSB");
				}

				PERFINFO_AUTO_STOP();
				return;
			}
		}
	}

	if(!source->emd->ignore3d)
	{
		sndSourceGetOrigin(source, pos);

		// If the source has moved or does not currently have a space assigned
		// determine what room (if any) the source exists within
		if(source->moved || !source->originSpace)
		{
			if(source->type == ST_ROOM && source->room.space)
			{
				newOrigin = source->room.space;
			}
			else
			{
				newOrigin = sndSpaceFind(pos);
			}
		}

		// the space was changed, so update the mixer accordingly
		// by either connecting the source's fmod event to the channel group (representing the room)
		// or re-assigning an existing fmod event to another channel group
		if(newOrigin && newOrigin != source->originSpace)
		{
			sndMixerSetSoundSourceSpace(soundMixer, source, newOrigin);
			source->moved = 1;
		}

		// If the source has been assigned a space AND it does not have a connection to the graph
		// we need to add it
		if(source->originSpace && !source->soundMixerChannel)
		{
			SoundMixerChannel *soundMixerChannel = source->originSpace->soundMixerChannel;
			if(soundMixerChannel)
			{
				// Make the connection to the DSP graph --- 
				sndMixerChannelAddSource(soundMixerChannel, source);
			}
		}

		sndSourcePreprocess(source);
	}

	// determine if the source is audible
	sndSourceCheckAudibility(source);

	// TODO:(gt) -- merge the following into sndSourceCheckAudibility
	if(!source->emd->ignore3d)
	{
		if(source->in_audible_space)
		{
			source->is_audible = 1;
		}
	}

	// ------- virtualization ----

	if(source->emd->ignore3d)
	{
		if(source->type == ST_POINT)
		{
			F32 radius;
			Vec3 listener_pos;

			FMOD_EventSystem_EventGetRadius(source->info_event, &radius);

			sndSourceGetOrigin(source, source->virtual_pos);
			sndSourceUpdatePosition(source);
			sndGetListenerPosition(listener_pos);

			source->distToListener = distance3(source->virtual_pos, listener_pos);

			if(source->emd->fade2d)
			{
				F32 mind = fmodEventGetMinRadius(source->info_event);
				F32 maxd = fmodEventGetMaxRadius(source->info_event);
				source->pseudoAttenuate = 1-(source->distToListener-mind)/(maxd-mind);
				MINMAX1(source->pseudoAttenuate, 0, 1);

				source->is_audible = source->distToListener < radius+SND_AUDIBLE_RADIUS;
			}
			else
			{
				// always audible
				source->is_audible = 1; 
			}
		}
	}
	else
	{
		SoundSpace *space = source->originSpace;
		U32 room = source->type == ST_ROOM;
		U32 local = space && space == soundMixer->currentSpace; //->is_current;
		Vec3 listener_pos;
		F32 radius;
		F32 dist;

		FMOD_EventSystem_EventGetRadius(source->info_event, &radius);
		sndGetListenerPosition(listener_pos);

		if(local)
		{
			if(space->multiplier!=1)
			{
				Vec3 dir;
				Vec3 space_pos;

				sndGetListenerSpacePos(space_pos);

				sndSourceGetOrigin(source, dir);
				subVec3(dir, space_pos, dir);

				dist = normalVec3(dir);
				dist *= space->multiplier;
				scaleAddVec3(dir, dist, space_pos, source->virtual_pos);
			}
			else
			{
				sndSourceGetOrigin(source, source->virtual_pos);
			}

			if(source->type==ST_ROOM)
			{
				source->directionality = 0;
			}
		}
		else // Non-local
		{
			sndSourceGetOrigin(source, source->virtual_pos);
		}


		distMoved = sndSourceUpdatePosition(source);


		if(room)
		{
			if(local)
			{
				source->distToListener = 0;
			}
			else
			{
				source->distToListener = sndMixerFindShortestDistanceToSpace(soundMixer, soundMixer->currentSpace, listener_pos, source->originSpace);

				source->directionality = source->distToListener / 25.0;
				CLAMP(source->directionality, 0.0, 1.0);
			}

			source->pseudoAttenuate = 1.0; // a room-tone is a special case, override if 2dFade enabled
		}
		else
		{
			if(source->emd && source->emd->clickie)
			{
				static Vec3 zeroVec = {0};

				source->distToListener = distance3(source->relative_pos, zeroVec);
			}
			else
			{
				source->distToListener = distance3(source->virtual_pos, listener_pos);
			}
			
			if(source->emd->fade2d)
			{
				F32 mind = fmodEventGetMinRadius(source->info_event);
				F32 maxd = fmodEventGetMaxRadius(source->info_event);
				source->pseudoAttenuate = 1-(source->distToListener-mind)/(maxd-mind);
				MINMAX1(source->pseudoAttenuate, 0, 1);
			}
		}


		shouldBeAudible = true;

		// Movement check
		// does this source care about movement?
		if(source->emd->moving >= 0) 
		{
			// yes, so determine if it is moving
			if(distMoved > 0.1)
			{
				// MOVED
				if(source->emd->moving == 0)
				{
					// sound needs to be stopped if it hasn't already
					shouldBeAudible = false;
				}
				else if(source->emd->moving == 1)
				{
					// sound needs to be started if it hasn't already
					shouldBeAudible = true;
				}
			} 
			else
			{
				// DID NOT MOVE

				// Do we update playback based on movement
				if(source->emd->moving == 1)
				{
					// sound needs to be stopped if it hasn't already
					shouldBeAudible = false;
				}
				else if(source->emd->moving == 0)
				{
					// sound needs to be started if it hasn't already
					shouldBeAudible = true;
				}
			}
		}


		if( (source->emd->ignore3d || source->distToListener < radius+SND_AUDIBLE_RADIUS) && shouldBeAudible )
		{
			source->is_audible = 1;
		}
		else
		{
			source->is_audible = 0;
		}
	}

	PERFINFO_AUTO_STOP();
}

void sndMixerUpdateSourceGroup(SoundMixer *soundMixer, SoundSourceGroup *group)
{
	sndUpdateSourceGroup(group, 0);
}


void sndMixerUpdateSources(SoundMixer *soundMixer)
{
	int numSources = eaSize(&space_state.sources);
	int numSourceGroups = eaSize(&space_state.source_groups);
	int i;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	// loop through ALL sources
	for(i = 0; i < numSources; i++)
	{
		SoundSource *source = space_state.sources[i];
		sndMixerUpdateSource(soundMixer, source);
	}

	for(i = 0; i < numSourceGroups; i++)
	{
		SoundSourceGroup *group = space_state.source_groups[i];
		sndMixerUpdateSourceGroup(soundMixer, group); // space_update?
	}

	PERFINFO_AUTO_STOP();
}

void sndMixerHelperAddReverbProperties(DSP_SfxReverb *dst, DSP_SfxReverb *src)
{
	dst->sfxreverb_drylevel += src->sfxreverb_drylevel;
	dst->sfxreverb_room += src->sfxreverb_room;
	dst->sfxreverb_roomhf += src->sfxreverb_roomhf;
	dst->sfxreverb_roomrollofffactor += src->sfxreverb_roomrollofffactor;
	dst->sfxreverb_decaytime += src->sfxreverb_decaytime;
	dst->sfxreverb_decayhfratio += src->sfxreverb_decayhfratio;
	dst->sfxreverb_reflectionslevel += src->sfxreverb_reflectionslevel;
	dst->sfxreverb_reflectionsdelay += src->sfxreverb_reflectionsdelay;
	dst->sfxreverb_reverblevel += src->sfxreverb_reverblevel;
	dst->sfxreverb_reverbdelay += src->sfxreverb_reverbdelay;
	dst->sfxreverb_diffusion += src->sfxreverb_diffusion;
	dst->sfxreverb_density += src->sfxreverb_density;
	dst->sfxreverb_hfreference += src->sfxreverb_hfreference;
	dst->sfxreverb_roomlf += src->sfxreverb_roomlf;
	dst->sfxreverb_lfreference += src->sfxreverb_lfreference;
}

void sndMixerHelperScaleReverbProperties(DSP_SfxReverb *dst, DSP_SfxReverb *src, F32 scale)
{
	dst->sfxreverb_drylevel = src->sfxreverb_drylevel * scale;
	dst->sfxreverb_room = src->sfxreverb_room * scale;
	dst->sfxreverb_roomhf = src->sfxreverb_roomhf * scale;
	dst->sfxreverb_roomrollofffactor = src->sfxreverb_roomrollofffactor * scale;
	dst->sfxreverb_decaytime = src->sfxreverb_decaytime * scale;
	dst->sfxreverb_decayhfratio = src->sfxreverb_decayhfratio * scale;
	dst->sfxreverb_reflectionslevel = src->sfxreverb_reflectionslevel * scale;
	dst->sfxreverb_reflectionsdelay = src->sfxreverb_reflectionsdelay * scale;
	dst->sfxreverb_reverblevel = src->sfxreverb_reverblevel * scale;
	dst->sfxreverb_reverbdelay = src->sfxreverb_reverbdelay * scale;
	dst->sfxreverb_diffusion = src->sfxreverb_diffusion * scale;
	dst->sfxreverb_density = src->sfxreverb_density * scale;
	dst->sfxreverb_hfreference = src->sfxreverb_hfreference * scale;
	dst->sfxreverb_roomlf = src->sfxreverb_roomlf * scale;
	dst->sfxreverb_lfreference = src->sfxreverb_lfreference * scale;
}

#define REVERB_XFADE_DISTANCE (30.0) // ft

SoundDSP* sndMixerGetDryReverb(void)
{
	static SoundDSP dsp;
	static int init = 0;

	if(!init)
	{
		init = 1;
		
		dsp.type = DSPSFXREVERB;
		ZeroStruct(&dsp.sfxreverb);
		dsp.sfxreverb.sfxreverb_drylevel = 0;
		dsp.sfxreverb.sfxreverb_reverblevel = -10000.0f;
		dsp.sfxreverb.sfxreverb_room = -10000.0f;
		dsp.sfxreverb.sfxreverb_roomhf = -10000.0f;
		dsp.sfxreverb.sfxreverb_roomlf = -10000.0f;
	}

	return &dsp;
}

SoundDSP* sndMixerGetNullDSP(DSPType dspt)
{
	static SoundDSP lowpass = {0};
	static SoundDSP slowpass = {0};
	static SoundDSP highpass = {0};
	static SoundDSP flange = {0};
	static SoundDSP chorus = {0};
	static SoundDSP normalize = {0};
	static int init = 0;

	if(init==0)
	{
		init = true;

		lowpass.type = DSPLOWPASS;
		lowpass.name = StructAllocString("NullLowpass");
		lowpass.lowpass.lowpass_cutoff = 22000;
		lowpass.lowpass.lowpass_resonance = 1.0;

		slowpass.type = DSPSLOWPASS;
		slowpass.name = StructAllocString("NullSlowpass");
		slowpass.slowpass.lowpass_cutoff = 220000;

		highpass.type = DSPHIGHPASS;
		highpass.name = StructAllocString("NullHighpass");
		highpass.highpass.highpass_cutoff = 1.0;
		highpass.highpass.highpass_resonance = 1.0;

		flange.type = DSPFLANGE;
		flange.name = StructAllocString("NullFlange");
		flange.flange.flange_depth = 0.01;
		flange.flange.flange_drymix = 1;
		flange.flange.flange_wetmix = 0;
		flange.flange.flange_rate = 0;

		normalize.type = DSPNORMALIZE;
		normalize.name = StructAllocString("NullNormalize");
		normalize.normalize.normalize_fadetime = 0;
		normalize.normalize.normalize_maxamp = 1.0;
		normalize.normalize.normalize_threshold = 0.0;
	}

	switch(dspt)
	{
		xcase DSPSFXREVERB: {
			return sndMixerGetDryReverb();
		}
		xcase DSPLOWPASS: {
			return &lowpass;
		}
		xcase DSPHIGHPASS: {
			return &highpass;
		}
		xcase DSPFLANGE: {
			return &flange;
		}
		xcase DSPNORMALIZE: {
			return &normalize;
		}
		xcase DSPSLOWPASS: {
			return &slowpass;
		}
		xdefault: Errorf("Unsupported DSP type requested: %d", dspt);
	}
	return NULL;
}

ParseTable* sndMixerGetDSPPTI(DSPType t)
{
	switch(t)
	{
		xcase DSPSFXREVERB:
			return parse_DSP_SfxReverb;
		xcase DSPLOWPASS:
			return parse_DSP_Lowpass;
		xcase DSPHIGHPASS:
			return parse_DSP_HighPass;
		xcase DSPFLANGE:
			return parse_DSP_Flange;
		xcase DSPNORMALIZE:
			return parse_DSP_Normalize;
		xcase DSPSLOWPASS:
			return parse_DSP_SLowpass;
		xdefault:
			Errorf("Unsupported DSP type requested: %d", t);
	}
	return NULL;
}

void sndMixerFadeDSP(SoundDSPEffect *effect)
{
	SoundDSP mixed = {0};
	SoundDSP *null = NULL;
	SoundDSP *target = GET_REF(effect->dsp_def);
	ParseTable *pti = NULL;
	int i;

	if(!target)
		return;

	null = sndMixerGetNullDSP(target->type);

	if(!null)
		return;

	pti = sndMixerGetDSPPTI(target->type);

	if(!pti)
		return;

	FORALL_PARSETABLE(pti, i)
	{
		if(TOK_GET_TYPE(pti[i].type)==TOK_F32_X)
		{
			F32 t = TokenStoreGetF32(pti, i, &target->sfxreverb, 0, NULL);
			F32 n = TokenStoreGetF32(pti, i, &null->sfxreverb, 0, NULL);
			F32 v = interpF32(effect->curinterp, n, t);

			TokenStoreSetF32(pti, i, &mixed.sfxreverb, 0, v, NULL, NULL);
		}
	}

	mixed.type = target->type;
	fmodDSPSetValuesByInfo(&mixed, effect->fmoddsp);
}

SoundDSPEffect* sndMixerAddDSPEffect(SoundMixer *mixer, SoundDSP *dsp)
{
	SoundDSPEffect *effect = NULL;

	FOR_EACH_IN_EARRAY(mixer->effects, SoundDSPEffect, test)
	{
		if(test->disconnectOnSource && !test->to_target && dsp==GET_REF(test->dsp_def))
		{
			// this shouldn't "pop" since the dsp definition is the same
			// and we're not tweaking test->curinterp
			test->disconnectOnSource = false;
			test->to_target = true;
			return test;
		}
	}
	FOR_EACH_END;

	effect = callocStruct(SoundDSPEffect);

	SET_HANDLE_FROM_REFERENT(space_state.dsp_dict, dsp, effect->dsp_def);
	effect->curinterp = 0;
	effect->to_target = true;
	effect->fmodcg = fmodChannelGroupCreate(effect, "Effect");
	effect->fmoddsp = fmodDSPCreateFromInfo(sndMixerGetNullDSP(dsp->type), effect);
	fmodChannelGroupAddDSP(effect->fmodcg, effect->fmoddsp);

	if(eaSize(&mixer->effects))
	{
		SoundDSPEffect *top = eaHead(&mixer->effects);
		fmodChannelGroupAddGroup(effect->fmodcg, top->fmodcg);
	}
	else
		fmodChannelGroupAddGroup(effect->fmodcg, mixer->fmodWetCG);

	eaInsert(&mixer->effects, effect, 0);

	return effect;
}

void sndMixerDelDSPEffect(SoundMixer *mixer, SoundDSPEffect **effect)
{
	if(!effect || !*effect)
		return;

	(*effect)->to_target = false;
	(*effect)->disconnectOnSource = true;

	*effect = NULL;
}

static void sndMixerDelDSPEffectInternal(SoundMixer *mixer, SoundDSPEffect *effect)
{
	int idx = 0;

	if(!effect)
		return;

	idx = eaFindAndRemove(&mixer->effects, effect);

	if (0 < idx)
	{
		if (idx == eaSize(&mixer->effects))
		{
			// last element was removed, relink the new last element
			fmodChannelGroupAddGroup(eaTail(&mixer->effects)->fmodcg, mixer->fmodWetCG);
		}
		else
		{
			// middle element was removed, link its neighbors
			assert(idx < eaSize(&mixer->effects));
			fmodChannelGroupAddGroup(mixer->effects[idx-1]->fmodcg, mixer->effects[idx]->fmodcg);
		}
	}
	// else first element was removed, don't need to relink channel groups

	sndFadeManagerRemove(g_audio_state.fadeManager, &effect->curinterp);
	fmodDSPFree(&effect->fmoddsp, true);
	fmodChannelGroupDestroy(&effect->fmodcg);
	REMOVE_HANDLE(effect->dsp_def);

	free(effect);
}

void sndMixerUpdateEffects(SoundMixer *mixer)
{
	FOR_EACH_IN_EARRAY(mixer->effects, SoundDSPEffect, effect)
	{
		if(effect->to_target)
			sndFadeManagerAdd(g_audio_state.fadeManager, &effect->curinterp, SFT_FLOAT, SND_STANDARD_FADE);
		else
			sndFadeManagerAdd(g_audio_state.fadeManager, &effect->curinterp, SFT_FLOAT, -SND_STANDARD_FADE);
		sndMixerFadeDSP(effect);

		if(effect->to_target==false && effect->curinterp==0 && effect->disconnectOnSource)
			sndMixerDelDSPEffectInternal(mixer, effect);
	}
	FOR_EACH_END;
}

void sndMixerUpdateReverbSettings(SoundMixer *soundMixer)
{
	// find nearest connector to listener
	Vec3 listenerPos;
	SoundSpaceConnector *soundSpaceConnector;
	SoundDSP *currentSpaceDSP;
	bool currentSpaceHasReverb;

	static SoundSpaceConnector **soundSpaceConnectors = NULL;

	if(!soundMixer->currentSpace) return;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	eaClear(&soundSpaceConnectors);

	// current space
	currentSpaceDSP = GET_REF(soundMixer->currentSpace->dsp_ref);
	currentSpaceHasReverb = currentSpaceDSP && currentSpaceDSP->type == DSPSFXREVERB;
	
	sndGetListenerSpacePos(listenerPos);
	if(sndMixerHelperConnectorsWithinDistance(soundMixer->currentSpace, listenerPos, REVERB_XFADE_DISTANCE, &soundSpaceConnectors))
	{
		int paramSetCount = 0;
		int numConnectors = eaSize(&soundSpaceConnectors);
		F32 dist;
		F32 distanceFactor;
		DSP_SfxReverb sfxReverb;
		DSP_SfxReverb accumReverbParams;
	
		bool adjacentSpaceHasReverb;
		SoundSpace *adjacentSpace;
		SoundDSP *adjacentSpaceDSP;

		memset(&accumReverbParams, 0, sizeof(DSP_SfxReverb)); // start clean

		if(eaSize(&soundSpaceConnectors) > 0)
		{
			soundSpaceConnector = soundSpaceConnectors[0]; // closest connector
			dist = soundSpaceConnector->cost;
			distanceFactor = CLAMP(dist / REVERB_XFADE_DISTANCE, 0.0, 1.0);

			adjacentSpace = sndSpaceConnectorOtherSpace(soundSpaceConnector, soundMixer->currentSpace);
			adjacentSpaceDSP = GET_REF(adjacentSpace->dsp_ref);
			adjacentSpaceHasReverb = adjacentSpaceDSP && adjacentSpaceDSP->type == DSPSFXREVERB;

			// x-fade the parameter sets
			if(currentSpaceHasReverb && adjacentSpaceHasReverb)
			{
				F32 scaleFactor = distanceFactor * 0.5 + 0.5;
				// accumulate properties by scale factor

				sndMixerHelperScaleReverbProperties(&sfxReverb, &currentSpaceDSP->sfxreverb, scaleFactor);
				sndMixerHelperAddReverbProperties(&accumReverbParams, &sfxReverb);

				scaleFactor = 1.0 - scaleFactor;
				sndMixerHelperScaleReverbProperties(&sfxReverb, &adjacentSpaceDSP->sfxreverb, scaleFactor);
				sndMixerHelperAddReverbProperties(&accumReverbParams, &sfxReverb);

				// write the params
				fmodDSPSetupSfxReverb(&accumReverbParams, soundMixer->fmodWetDSP);
			} 
			else if(adjacentSpaceHasReverb)
			{
				SoundDSP *soundDSP = GET_REF(adjacentSpace->dsp_ref);

				fmodDSPSetValuesByInfo(soundDSP, soundMixer->fmodWetDSP);
			}
			else if(currentSpaceHasReverb)
			{
				SoundDSP *soundDSP = GET_REF(soundMixer->currentSpace->dsp_ref);

				fmodDSPSetValuesByInfo(soundDSP, soundMixer->fmodWetDSP);
			}
			else
			{
				fmodDSPSetValuesByInfo(sndMixerGetDryReverb(), soundMixer->fmodWetDSP);
			}
		}
	}
	else
	{
		// no connectors within range
		if(currentSpaceHasReverb)
		{
			// set the properties from current space
			SoundDSP *soundDSP = GET_REF(soundMixer->currentSpace->dsp_ref);

			fmodDSPSetValuesByInfo(soundDSP, soundMixer->fmodWetDSP);
		}
		else
		{
			// turn off the reverb
			fmodDSPSetValuesByInfo(sndMixerGetDryReverb(), soundMixer->fmodWetDSP);
		}
	}

	PERFINFO_AUTO_STOP();
}

void sndMixerUpdateMusic(SoundMixer *soundMixer, bool spaceChanged)
{
	PERFINFO_AUTO_START("sndMixerUpdateMusic", 1);

	if(spaceChanged)
	{
		bool okayToPlay = true;

		// are we currently in a cut-scene?
		if(g_audio_state.cutscene_active_func) 
		{
			if(g_audio_state.cutscene_active_func())
			{
				// yes, so do not trigger new music
				okayToPlay = false;
			}
		}

		if(okayToPlay)
		{
			// play music (if any)
			if(soundMixer->currentSpace->music_name && soundMixer->currentSpace->music_name[0])
			{
				sndMusicPlayWorld(soundMixer->currentSpace->music_name, soundMixer->currentSpace->obj.file_name);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void sndMixerUpdateConnections(SoundMixer *soundMixer)
{
	PERFINFO_AUTO_START("sndMixerUpdateConnections", 1);
	if(space_state.needs_rebuild)
	{
		// Find spaces for SoundSpaceConnectors
		int numSpaces, i;

		numSpaces = eaSize(&soundMixer->spaces);
		for(i = 0; i < numSpaces; i++)
		{
			SoundSpace *space = soundMixer->spaces[i];
			eaClear(&space->connectors);
		}

		for(i =0 ; i < eaSize(&space_state.global_conns); i++)
		{
			SoundSpaceConnector *conn = space_state.global_conns[i];
			sndSpaceConnectorFindSpaces(conn);
			sndSpaceConnectorReleaseEdges(conn);
		}

		// Create Connection Graph
		// construct all edges
		for(i = 0; i < numSpaces; i++)
		{
			int j, numConnectors;
			SoundSpace *soundSpace = soundMixer->spaces[i];

			numConnectors = eaSize(&soundSpace->connectors);

			for(j = 0; j < numConnectors; j++)
			{
				SoundSpaceConnector *soundSpaceConnector = soundSpace->connectors[j];
				sndSpaceConnectorFindConnectorsInSpace(soundSpaceConnector, soundSpace);
			}
		}

		// we need to update our cache
		sndMixerSetDistanceCacheNeedsUpdate(soundMixer, true);

		// make sure all of our "soundspheres" know a change has been made
		// their origin may need to be updated
		for(i = eaSize(&space_state.non_exclusive_spaces)-1; i >= 0; i--)
		{
			SoundSpace *soundSpace = space_state.non_exclusive_spaces[i];
			int j;
			if(soundSpace->ownedSources)
			{
				for(j = eaSize(&soundSpace->ownedSources)-1; j >= 0; j--)
				{
					SoundSource *source = soundSpace->ownedSources[j];
					source->moved = 1;
				}
			}
		}

		space_state.needs_rebuild = 0;
	}
	PERFINFO_AUTO_STOP();
}

void sndMixerSetMaxPlaybacks(SoundMixer *soundMixer, int maxPlaybacks)
{
	soundMixer->maxPlaybacks = maxPlaybacks;
}

int sndMixerMaxPlaybacks(SoundMixer *soundMixer)
{
	return soundMixer->maxPlaybacks;
}

U32 sndMixerPlayEvent(SoundMixer *soundMixer, SoundSource *source)
{
	U32 result = FMOD_OK;
	bool okayToPlay = false;

	int numEventsPlaying = sndMixerNumEventsPlaying(soundMixer);
	// stealing can be disabled if maxPlaybacks set to 0
	if(soundMixer->maxPlaybacks > 0 && numEventsPlaying >= soundMixer->maxPlaybacks)
	{
		int i;
		bool stole = false;

		// we need to steal
		// grab oldest steal-able voice
		for(i = 0; i < numEventsPlaying; i++)
		{
			FmodEventTracker *fmodEventTracker = soundMixer->eaPlayingEvents[i];
			if(fmodEventTracker->canSteal && !fmodEventTracker->stolen)
			{
				SoundSource *stolenSource;
				fmodEventGetUserData(fmodEventTracker->fmodEvent, &stolenSource);
				if(stolenSource)
				{
					stolenSource->needs_stop = 1; // flag the event - it needs to be stopped
					stole = true;
					fmodEventTracker->stolen = true;
					break;
				}
			}
		}

		if(stole)
		{
			okayToPlay = true;
		}
	}
	else
	{
		okayToPlay = true;
	}

	if(okayToPlay
		|| fmodEventIsLooping(source->fmod_event) // always play looping sounds
		|| SAFE_MEMBER2(source,emd,alwaysAssignVoice))
	{
		// we're getting invalid handles here occasionally, these should probably be renewed instead of left as bad data
		result = FMOD_EventSystem_PlayEvent(source->fmod_event);

		if(!source->emd || source->emd->use_dsp)
		{
			if(source->soundMixerChannel)
			{
				sndMixerChannelConnectSource(source->soundMixerChannel, source);
			}
		}
	}
	else
	{
		soundMixer->numIgnoredEvents++;
	}

	return result;
}

void sndMixerIncEventsSkippedDueToMemory(SoundMixer *soundMixer)
{
	soundMixer->numSkippedDueToMemory++;
}

int sndMixerEventsSkippedDueToMemory(SoundMixer *soundMixer)
{
	return soundMixer->numSkippedDueToMemory;
}

int sndMixerNumIgnoredEvents(SoundMixer *soundMixer)
{
	return soundMixer->numIgnoredEvents;
}

int sndMixerNumEventsPlaying(SoundMixer *soundMixer)
{
	return eaSize(&soundMixer->eaPlayingEvents);
}

S32 sndMixerHelperSortFmodEventTracker(const FmodEventTracker **left, const FmodEventTracker **right)
{
	const FmodEventTracker *l = *left;
	const FmodEventTracker *r = *right;

	return l->timestamp - r->timestamp;
}

void sndMixerTrackEvent(SoundMixer *soundMixer, void *fmodEvent)
{
	FmodEventTracker *fmodEventTracker;
	EventMetaData *emd;

	MP_CREATE(FmodEventTracker, 32);

	fmodEventTracker = MP_ALLOC(FmodEventTracker);

	emd = sndFindMetaData(fmodEvent);

	fmodEventTracker->fmodEvent = fmodEvent;
	fmodEventTracker->timestamp = timerCpuTicks();
	if(emd)
	{
		fmodEventTracker->canSteal = emd->type == SND_FX;
	}

	// assert(	!eaSize(&soundMixer->eaPlayingEvents) ||
	//			soundMixer->eaPlayingEvents[eaSize(&soundMixer->eaPlayingEvents)-1]->timestamp <= fmodEventTracker->timestamp);

	eaPush(&soundMixer->eaPlayingEvents, fmodEventTracker);
}

void sndMixerStopTrackingEvent(SoundMixer *soundMixer, void *fmodEvent)
{
	int i;
	SoundSource *source = NULL;

	for(i = eaSize(&soundMixer->eaPlayingEvents)-1; i >= 0; i--)
	{
		FmodEventTracker *fmodEventTracker = soundMixer->eaPlayingEvents[i];
		if(fmodEventTracker->fmodEvent == fmodEvent)
		{
			eaFindAndRemove(&soundMixer->eaPlayingEvents, fmodEventTracker);
			MP_FREE(FmodEventTracker, fmodEventTracker);
			break;
		}
	}
}

void sndMixerStopTrackingAllEvents(SoundMixer *soundMixer)
{
	int i;
	SoundSource *source = NULL;

	for(i = eaSize(&soundMixer->eaPlayingEvents)-1; i >= 0; i--)
	{
		FmodEventTracker *fmodEventTracker = soundMixer->eaPlayingEvents[i];
		MP_FREE(FmodEventTracker, fmodEventTracker);
	}
	
	eaClear(&soundMixer->eaPlayingEvents);
}

void sndMixerSetFxDuckingEnabled(SoundMixer *soundMixer, bool enabled)
{
	soundMixer->bFxDuckingEnabled = enabled;
}

bool sndMixerIsFxDuckingEnabled(SoundMixer *soundMixer)
{
	return soundMixer->bFxDuckingEnabled;
}

void sndMixerSetFxDuckRate(SoundMixer *soundMixer, F32 rate) 
{
	soundMixer->fxDuckRate = rate; // measured in percent per sec
}

F32 sndMixerFxDuckRate(SoundMixer *soundMixer) 
{ 
	return soundMixer->fxDuckRate; 
}

void sndMixerSetDuckNumEventThreshold(SoundMixer *soundMixer, int numEventThreshold) 
{
	soundMixer->fxDuckNumEventThreshold = numEventThreshold;
}

int sndMixerFxDuckNumEventThreshold(SoundMixer *soundMixer)
{
	return soundMixer->fxDuckNumEventThreshold;
}

void sndMixerSetDuckScaleTarget(SoundMixer *soundMixer, F32 target)
{
	soundMixer->fxDuckScaleTarget = target;
}

F32 sndMixerFxDuckScaleTarget(SoundMixer *soundMixer)
{
	return soundMixer->fxDuckScaleTarget;
}

void sndMixerUpdateFxDucking(SoundMixer *soundMixer)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if( sndMixerIsFxDuckingEnabled(soundMixer) )
	{
		if(soundMixer->lastUpdatedTime > 0)
		{
			F32 inc;
			F32 val;
			F32 timeDelta;

			int numEventsPlaying = sndMixerNumEventsPlaying(soundMixer);
			timeDelta = timerSeconds(timerCpuTicks() - soundMixer->lastUpdatedTime);

			inc = soundMixer->fxDuckRate * timeDelta;
			val = g_audio_state.fxDuckScaleFactor;

			// are we above or below the threshold
			if( numEventsPlaying > sndMixerFxDuckNumEventThreshold(soundMixer) )
			{
				// above threshold
				val -= inc;
			}
			else
			{
				// below
				val += inc;
			}
			g_audio_state.fxDuckScaleFactor = CLAMP(val, soundMixer->fxDuckScaleTarget, 1.0);
		}
	}
	else
	{
		g_audio_state.fxDuckScaleFactor = 1.0; // 1.0 = no ducking
	}

	PERFINFO_AUTO_STOP();
}


void sndMixerOncePerFrame(SoundMixer *soundMixer, F32 elapsed)
{
	bool spaceChanged;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	// rebuild connections if necessary
	sndMixerUpdateConnections(soundMixer);

	// determine which space we're in
	spaceChanged = sndMixerUpdateCurrentSpace(soundMixer);
	if(spaceChanged)
	{
		SoundDSP *spacedsp = NULL;
		sndMixerSetDistanceCacheNeedsUpdate(soundMixer, true);

		if(soundMixer->spaceEffect)
			sndMixerDelDSPEffect(soundMixer, &soundMixer->spaceEffect);

		spacedsp = GET_REF(soundMixer->currentSpace->dsp_ref);
		if(soundMixer->currentSpace && spacedsp && spacedsp->type!=DSPSFXREVERB)
			soundMixer->spaceEffect = sndMixerAddDSPEffect(soundMixer, spacedsp);
	}

	// determine if we need to update music 
	sndMixerUpdateMusic(soundMixer, spaceChanged);

	// determine which spaces are audible
	sndMixerUpdateAudibleSpaces(soundMixer);
	
	// update all sound sources' state
	sndMixerUpdateSources(soundMixer);

	// update global reverb params & return level
	sndMixerUpdateReverbSettings(soundMixer);
	sndMixerUpdateEffects(soundMixer);

	// Updating Fx Ducking
	sndMixerUpdateFxDucking(soundMixer);

	soundMixer->lastUpdatedTime = timerCpuTicks();

	PERFINFO_AUTO_STOP();
}

extern SoundMixer *gSndMixer;
static SoundDSPEffect *gTestEffect = NULL;
AUTO_COMMAND;
void sndTestDSPEffectAdd(ACMD_NAMELIST("SoundDSPs", REFDICTIONARY) const char* name)
{
	SoundDSP *dsp = RefSystem_ReferentFromString(space_state.dsp_dict, name);
	gTestEffect = sndMixerAddDSPEffect(gSndMixer, dsp);
}

AUTO_COMMAND;
void sndTestDSPEffectDel(void)
{
	sndMixerDelDSPEffect(gSndMixer, &gTestEffect);
}

#endif
