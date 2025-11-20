#ifndef STUB_SOUNDLIB
#include "sndCluster.h"

// Cryptic Structs
#include "earray.h"
#include "estring.h"
#include "mathutil.h"

// Sound structs
#include "event_sys.h"
#include "sndSource.h"

#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

void sndClusterRemoveAllSources(SoundSourceCluster *cluster);
void sndClusterDestroy(SoundSourceCluster *cluster);

void sndClustersInit(SoundSourceClusters *clusters, F32 thresholdDistance)
{
	assert(thresholdDistance > 0.0);

	clusters->clusterDistanceThreshold = thresholdDistance;
}

// on input
//		inOutClosestSource - if NOT null inOutDistance will be evaluated
// on output
//		if there are sources to test OR inOutClosestSource was not NULL on input
//		inOutClosestSource will be set
//		as well as the distance
bool sndClusterClosestSource(SoundSource **sources, SoundSource *source, SoundSource **inOutClosestSource, F32 *inOutDistance)
{
	bool result = false;
	int numSources = eaSize(&sources);
	if(numSources > 0)
	{
		int i;
		F32 dist;
		F32 minDistance;
		SoundSource *tmpSource = NULL;
		SoundSource *testSource = sources[0];
		Vec3 testPos = {0};

		if(testSource->cluster)
		{
			copyVec3(testSource->cluster->midPoint, testPos);
		}
		else
		{
			copyVec3(testSource->point.pos, testPos);
		}

		dist = distance3(source->point.pos, testPos);
		tmpSource = testSource;

		minDistance = dist;
		tmpSource = testSource;

		// were we passed an input to check?
		if(*inOutClosestSource != NULL)
		{
			if(*inOutDistance < minDistance)
			{
				minDistance = *inOutDistance;
				tmpSource = *inOutClosestSource;
			}
		}

		for(i = 1; i < numSources; i++)
		{
			testSource = sources[i];
			if(testSource->cluster)
			{
				copyVec3(testSource->cluster->midPoint, testPos);
			}
			else
			{
				copyVec3(testSource->point.pos, testPos);
			}

			dist = distance3(source->point.pos, testPos);
			if(dist < minDistance)
			{
				minDistance = dist;
				tmpSource = testSource;
			}
		}

		*inOutClosestSource = tmpSource;
		*inOutDistance = minDistance;

		result = true;
	}
	else
	{
		// we were passed an input, so just returning the input as the output
		if(*inOutClosestSource != NULL)
		{
			result = true;
		}
	}
	return result;
}

void sndClustersRemoveSource(SoundSourceClusters *clusters, SoundSource *source)
{
	assert(clusters);
	assert(source);

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(source->cluster)
	{
		SoundSourceCluster *cluster = source->cluster;
		sndClusterRemoveSource(cluster, source);

		// should this cluster be removed?
		if(cluster->cleanUp || eaSize(&cluster->sources) == 0)
		{
			sndClusterDestroy(cluster);
		}
	}

	PERFINFO_AUTO_STOP();
}

void sndClusterAddValidSources(SoundSource **sources, SoundSource ***dstSources)
{
	int i;
	for(i = eaSize(&sources)-1; i >= 0; --i) {
		SoundSource *source = sources[i];
		if(!source->clean_up)
		{
			eaPush(dstSources, sources[i]);
		}
	}
}

void sndClusterCompileValidSourcesForGroup(SoundSourceGroup *group, SoundSource *source, SoundSource ***groupSources)
{
	eaClear(groupSources);

	sndClusterAddValidSources(group->inactive_sources, groupSources);
	sndClusterAddValidSources(group->active_sources, groupSources);
	sndClusterAddValidSources(group->dead_sources, groupSources);

	eaFindAndRemoveFast(groupSources, source); // make sure we don't test our own source
}

void sndClustersAddSource(SoundSourceClusters *clusters, SoundSource *source)
{
	SoundSource *closestSource = NULL;
	F32 distance = -1;
	int loops;
	static SoundSource **groupSources = NULL;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	assert(clusters);
	assert(source);
	assert(source->group);

	source->cluster = NULL; // make sure this is properly init'd

	if(!source->info_event) 
	{ 
		PERFINFO_AUTO_STOP();
		// we don't know what this is, so just leave it alone
		return;
	}
	
	if( source->type != ST_POINT ) 
	{
		PERFINFO_AUTO_STOP();
		return; // must be a point-source, i.e., ignore music and room tones
	}

	// it must be a sustaining sound
	FMOD_EventSystem_EventIsLooping(source->info_event, &loops);
	if(loops)
	{
		SoundSourceGroup *group = source->group;

		// compile all group sources into one array
		sndClusterCompileValidSourcesForGroup(group, source, &groupSources);
	
		if(sndClusterClosestSource(groupSources, source, &closestSource, &distance))
		{
			if(distance < clusters->clusterDistanceThreshold)
			{
				// we got one within range...
				// does the closest source have a cluster?
				if(closestSource->cluster)
				{
					// yes, so add the source to the cluster
					sndClusterAddSource(closestSource->cluster, source);
				}
				else
				{
					// nope so create a cluster for the two sources
					SoundSourceCluster *cluster = sndClusterCreate(clusters, closestSource);
					sndClusterAddSource(cluster, source);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

SoundSourceCluster* sndClusterCreate(SoundSourceClusters *clusters, SoundSource *soundingSource)
{
	SoundSourceCluster *cluster;
	assert(soundingSource);

	cluster = calloc(1, sizeof(SoundSourceCluster));
	cluster->soundingSource = soundingSource;
	cluster->cleanUp = 0;

	cluster->parentClusters = clusters;
	eaPush(&clusters->clusters, cluster);

	sndClusterAddSource(cluster, soundingSource);

	return cluster;
}

void sndClusterDestroy(SoundSourceCluster *cluster)
{
	assert(cluster);

	sndClusterRemoveAllSources(cluster);

	eaDestroy(&cluster->sources);

	// unlink from parent struct
	if(cluster->parentClusters)
	{
		eaFindAndRemove(&cluster->parentClusters->clusters, cluster);
	}
	
	free(cluster);
}

void sndClusterUpdateMidPoint(SoundSourceCluster *cluster)
{
	int i, numSources;
	Vec3 sum = {0};

	// calculate mid-point
	numSources = eaSize(&cluster->sources);

	if(numSources > 0)
	{
		int count = 0;
		F32 scale;
		for(i = 0; i < numSources; i++)
		{
			SoundSource *source = cluster->sources[i];
			if(source->cluster && !source->clean_up)
			{
				addVec3(source->point.pos, sum, sum);	
				count++;
			}
		}
		if(count > 0)
		{
			scale = 1.0 / (F32)count;

			sum[0] *= scale;
			sum[1] *= scale;
			sum[2] *= scale;

			copyVec3(sum, cluster->midPoint);
		}
	}
}

void sndClusterAddSource(SoundSourceCluster *cluster, SoundSource *source)
{
	assert(source);
	assert(source->cluster == NULL);

	source->cluster = cluster; // set the cluster for the source

	eaPush(&cluster->sources, source);

	sndClusterUpdateMidPoint(cluster);
}

// removeFromList - determines whether the source is unlinked from the cluster's source list 
// (see sndClusterRemoveAllSources)
void sndClusterRemoveSourceEx(SoundSourceCluster *cluster, SoundSource *source, bool removeFromList)
{
	bool destroyCluster = false;
	assert(source);

	source->cluster = NULL;

	if(cluster->soundingSource == source)
	{
		int i;
		int numSources = eaSize(&cluster->sources);
		SoundSource *newSoundingSource = NULL;

		// we're removing the sounding source, so we need to replace
		for(i = 0; i < numSources; i++)
		{
			// it must be a source with a valid cluster (i.e., not already removed) 
			// AND it must not be the current sounding source
			if(cluster->sources[i] != source && cluster->sources[i]->cluster == cluster)
			{
				newSoundingSource = cluster->sources[i];
				break;
			}
		}

		if(newSoundingSource)
		{
			cluster->soundingSource = newSoundingSource;
		}
		else
		{
			cluster->cleanUp = 1;
		}
	}

	sndClusterUpdateMidPoint(cluster);

	if(removeFromList) eaFindAndRemove(&cluster->sources, source);
}

void sndClusterRemoveSource(SoundSourceCluster *cluster, SoundSource *source)
{
	sndClusterRemoveSourceEx(cluster, source, true);
}

void sndClusterRemoveAllSources(SoundSourceCluster *cluster)
{
	int i;
	for(i = eaSize(&cluster->sources)-1; i >= 0; --i)
	{
		sndClusterRemoveSourceEx(cluster, cluster->sources[i], false);
	}
	eaClear(&cluster->sources);
}

#endif
