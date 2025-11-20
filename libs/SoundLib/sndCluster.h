/***************************************************************************



***************************************************************************/
// Author - Greg Thompson

#pragma once

#ifndef _SNDCLUSTER_H
#define _SNDCLUSTER_H
GCC_SYSTEM

#include "stdtypes.h"
#include "ReferenceSystem.h"

typedef struct SoundSource SoundSource;
typedef struct SoundSourceClusters SoundSourceClusters;

// structure representing a 'cluster' of SoundSources
// A cluster is a collection of sources in which only one is actively 'sounding'
// The cluster maintains a  midPoint of all sources in the 'cluster'.  
// All sources must be within a certain distance of each other
typedef struct SoundSourceCluster {
	SoundSource **sources;
	SoundSource *soundingSource;
	SoundSourceClusters *parentClusters;

	Vec3 midPoint;
	
	U32 cleanUp : 1;
} SoundSourceCluster;


typedef struct SoundSourceClusters {
	SoundSourceCluster **clusters;

	F32 clusterDistanceThreshold;
} SoundSourceClusters;


//
// SoundSourceClusters
//

//! init a clusters struct
void sndClustersInit(SoundSourceClusters *clusters, F32 thresholdDistance);

//! check to see if source should be clustered (if so, cluster)
// All SoundSources will be run through this check, so the sndCluster system will maintain its own list of sources
void sndClustersAddSource(SoundSourceClusters *clusters, SoundSource *source);

//! remove from the master list
// check to see if source has cluster (and remove accordingly)
void sndClustersRemoveSource(SoundSourceClusters *clusters, SoundSource *source);

//
// SoundSourceCluster
//

//! create a cluster
SoundSourceCluster* sndClusterCreate(SoundSourceClusters *clusters, SoundSource *ownerSource);

//! add a source to a cluster
void sndClusterAddSource(SoundSourceCluster *cluster, SoundSource *source);

//! remove a source from a cluster
void sndClusterRemoveSource(SoundSourceCluster *cluster, SoundSource *source);

#endif
