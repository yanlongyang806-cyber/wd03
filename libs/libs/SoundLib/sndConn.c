/***************************************************************************



***************************************************************************/

#ifndef STUB_SOUNDLIB

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "FolderCache.h"
#include "MemoryPool.h"
#include "timing.h"

#include "sndLibPrivate.h"
#include "event_sys.h"

#include "gDebug.h"
#include "sndConn.h"
#include "sndDebug2.h"
#include "sndSource.h"
#include "sndSpace.h"
#include "bounds.h"

// For Volume query chaches
#include "entEnums.h"
#include "MapDescription.h"
#include "WorldGrid.h"
#include "wlVolumes.h"

// Room system!
#include "RoomConn.h"

// For pointLineDist
#include "LineDist.h"

// For ctriCollide
#include "../wcoll/collide.h"

#define SSC_CLASS_NAME "SoundSpaceConnector"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

MP_DEFINE(SoundSpaceConnector);

StashTable sndConnErrors;				// int stash of hashes, I don't really care about collisions much and storing all those strings...
StashTable sndConnZeroRangeErrors;		// same


//typedef U32 (*SoundObjectMsgHandler)(SoundObject *obj, SoundObjectMsg *msg);
U32 sndConnObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg)
{
	SoundSpaceConnector *conn = (SoundSpaceConnector*)obj;

	switch(msg->type)
	{
		xcase SNDOBJMSG_INIT: {

		}
		xcase SNDOBJMSG_MEMTRACKER: {
			
		}
		xcase SNDOBJMSG_DESTROY: {
			
		}
		xcase SNDOBJMSG_CLEAN: {
			
		}
	}
	return 0;
}

SoundSpace* sndConnGetOther(SoundSpaceConnector *conn, SoundSpace *space)
{
	return conn->space1==space ? conn->space2 : conn->space1;
}

SoundSpace* sndConnGetSource(SoundSpaceConnector *conn)
{
	return conn->to_1 ? conn->space2 : conn->space1;
}

SoundSpace* sndConnGetTarget(SoundSpaceConnector *conn)
{
	return !conn->to_1 ? conn->space2 : conn->space1;
}

U32 sndConnIsSpaceSource(SoundSpaceConnector *conn, SoundSpace *source)
{
	return sndConnGetSource(conn) == source;
}

U32 sndConnIsSpaceTarget(SoundSpaceConnector *conn, SoundSpace *source)
{
	return sndConnGetTarget(conn) == source;
}

void sndConnSetTarget(SoundSpaceConnector *conn, SoundSpace *space)
{
	U32 old_to_1 = conn->to_1;
	conn->to_1 = conn->space1 == space;
}

void sndConnSetSource(SoundSpaceConnector *conn, SoundSpace *space)
{
	U32 old_to_1 = conn->to_1;
	conn->to_1 = !(conn->space1 == space);
}

F32 sndConnPointDist(const SoundSpaceConnector *conn, const Vec3 p, Vec3 coll)
{
	return boxPointNearestPoint(conn->local_min, conn->local_max, conn->world_mat, NULL, p, coll);
}

F32 sndConnSimplePointDist(const SoundSpaceConnector *conn, const Vec3 pt)
{
	return distance3XZ(conn->world_mid, pt);
}

SoundSpaceConnectorProperties* sndConnGetSourceProps(SoundSpaceConnector *conn)
{
	return conn->to_1 ? &conn->props2 : &conn->props1;
}

SoundSpaceConnectorProperties* sndConnGetTargetProps(SoundSpaceConnector *conn)
{
	return !conn->to_1 ? &conn->props2 : &conn->props1;
}

SoundSpaceConnectorProperties* sndConnGetOtherProps(SoundSpaceConnector *conn, SoundSpace *space)
{
	return conn->space1 == space ? &conn->props2 : &conn->props1;
}

SoundSpaceConnectorProperties* sndConnGetProps(SoundSpaceConnector *conn, SoundSpace *space)
{
	return conn->space1 == space ? &conn->props1 : &conn->props2;
}

F32 pointSquareDist(Vec3 point, Vec3 min, Vec3 max, Mat4 mat)
{
	Vec3 col;
	return pointLineDistSquared(point, min, max, col);
}

SoundSpaceConnectorProperties* sndConnGetSpaceProperties(SoundSpaceConnector *conn, SoundSpace *space)
{
	if(conn->space1==space)
	{
		return &conn->props1;
	}
	if(conn->space2==space)
	{
		return &conn->props2;
	}
	return NULL;
}

F32 sndConnConnDist(SoundSpaceConnector *conn1, SoundSpaceConnector *conn2)
{
	return distance3(conn1->world_mid, conn2->world_mid);
}

F32 sndConnGetAudibility(SoundSpaceConnectorProperties *props, F32 dist)
{
	return (dist-props->attn_0db_range) / (props->attn_max_range-props->attn_0db_range);
}

void sndConnTransmissionCreate(SoundSpaceConnector *source, SoundSpaceConnector *target, SoundSpace *space, F32 dist)
{
	SoundSpaceConnectorProperties *props_src = sndConnGetSpaceProperties(source, space);
	SoundSpaceConnectorProperties *props_tar = sndConnGetSpaceProperties(target, space);

	SoundSpaceConnectorTransmission *trans = NULL;
	trans = callocStruct(SoundSpaceConnectorTransmission);

	trans->audibility = sndConnGetAudibility(props_src, dist);
	MAX1(trans->audibility, 0);
	trans->conn = source;

	eaPush(&props_tar->audibleConns, trans);
}

U32 sndConnGetWhichSpace(SoundSpaceConnector *conn, SoundSpace *space)
{
	return conn->space1 == space;
}

U32 sndConnConnectsSpace(SoundSpaceConnector *conn, SoundSpace *space)
{
	return conn->space1 == space || conn->space2 == space;
}

SoundSpaceConnector* sndConnCreate(RoomPortal *portal)
{
	SoundSpaceConnector *conn = NULL;
	const char *dsp;

	if (g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return NULL;
	}

	if(portal->sound_conn)
	{
		return portal->sound_conn;
	}
	
	if(portal->neighbor && portal->neighbor->sound_conn)
	{
		conn = portal->neighbor->sound_conn;

		conn->portal1 = portal;
		conn->portal2 = portal->neighbor;
		portal->sound_conn = conn;

		space_state.needs_rebuild = 1;

		return conn;
	}
	

	MP_CREATE(SoundSpaceConnector, 10);
	conn = MP_ALLOC(SoundSpaceConnector);
	
	//sndPrintf(2, SNDDBG_SPACE, "sndConnCreate alloc conn : %p  assigning to portal: %p %s\n", conn, portal, portal->def_name);

	conn->portal1 = portal;
	conn->portal2 = portal->neighbor;
	portal->sound_conn = conn;

	if(portal->neighbor)
		portal->neighbor->sound_conn = conn;

	mulBoundsAA(portal->bounds_min, 
				portal->bounds_max, 
				portal->world_mat, 
				conn->world_min, 
				conn->world_max);
	centerVec3(conn->world_min, conn->world_max, conn->world_mid);

	copyVec3(portal->bounds_min, conn->local_min);
	copyVec3(portal->bounds_max, conn->local_max);
	copyMat4(portal->world_mat, conn->world_mat);

	conn->props1.attn_0db_range = conn->portal1->sound_conn_props->min_range;
	conn->props1.attn_max_range = conn->portal1->sound_conn_props->max_range;
	if(conn->portal2)
	{
		conn->props2.attn_0db_range = conn->portal2->sound_conn_props->min_range;
		conn->props2.attn_max_range = conn->portal2->sound_conn_props->max_range;
	}
	else
		conn->props2 = conn->props1;
		
	conn->obj.fmod_channel_group = NULL;
	sndObjectCreateByName(&conn->obj, SSC_CLASS_NAME, layerGetFilename(portal->layer), portal->def_name, "GROUPTREE");

	dsp = portal->sound_conn_props->dsp_name;
	if(dsp && dsp[0])
	{
		SET_HANDLE_FROM_STRING(space_state.dsp_dict, dsp, conn->dsp_ref);
	}

	eaPush(&space_state.global_conns, conn);
	if(g_audio_dbg.debugging)
	{
		gDebuggerObjectAddObject(g_audio_dbg.spaces, g_audio_dbg.ssc_type, conn, conn->debug_object);
	}

	space_state.needs_rebuild = 1;

	return conn;
}

static void sndConnCleanSpace(SoundSpaceConnector *conn, SoundSpace *space)
{
	int i;
	
	//sndPrintf(2, SNDDBG_SPACE, "sndConnCleanSpace conn: %p space: %p\n", conn, space);

	if(conn->space1!=space && conn->space2!=space)
	{
		return;
	}

	for(i=0; i<eaSize(&space->connectors); i++)
	{
		int j;
		SoundSpaceConnector *other = space->connectors[i];
		SoundSpaceConnectorProperties *props;

		devassertmsg(other!=conn, "Found already removed connector back in space");

		props = sndConnGetSpaceProperties(other, space);

		if(!props)
		{
			continue;		// Probably a just deleted space
		}

		for(j=eaSize(&props->audibleConns)-1; j>=0; j--)
		{
			SoundSpaceConnectorTransmission *trans = props->audibleConns[j];
			if(trans->conn==conn)
			{
				eaRemoveFast(&props->audibleConns, j);
				free(trans);
			}
		}
	}
}

void sndConnDestroy(RoomPortal *portal)
{
	int i;
	SoundSpace *srcspace = NULL;
	SoundSpaceConnector *conn = portal->sound_conn;

	//sndPrintf(2, SNDDBG_SPACE, "sndConnDestroy portal : %p %s\n", portal, portal->def_name);

	if(!portal->sound_conn)
	{
		//sndPrintf(2, SNDDBG_SPACE, " no sound_conn\n");

		return;
	}

	if(conn->destroyed)
	{
		return;
	}

	conn->destroyed = 1;

	srcspace = sndConnGetSource(conn);
	if(srcspace)
	{
		srcspace->is_audible = 0;
	}
	
	if(conn->space1)
	{
		eaFindAndRemoveFast(&conn->space1->connectors, conn);

		sndConnCleanSpace(conn, conn->space1);
	}
	if(conn->space2)
	{
		eaFindAndRemoveFast(&conn->space2->connectors, conn);

		sndConnCleanSpace(conn, conn->space2);
	}

	//eaPush(&space_state.destroyed.conns, conn);

	//sndPrintf(2, SNDDBG_SPACE, " remove conn : %p from global_conns (size: %d)\n", conn, eaSize(&space_state.global_conns));

	eaFindAndRemoveFast(&space_state.global_conns, conn);

	devassert(conn->destroyed);
	devassert(GetCurrentThreadId()==g_audio_state.main_thread_id);

	for(i=eaSize(&conn->transmitted)-1; i>=0; i--)
	{
		SoundSource *source = conn->transmitted[i];

		sndSourceSetConnToListener(source, NULL);
	}
	eaDestroy(&conn->transmitted);

	REMOVE_HANDLE(conn->dsp_ref);

	sndObjectDestroy(&conn->obj);
	gDebuggerObjectRemove(conn->debug_object);

	conn->portal1->sound_conn = NULL;
	if(conn->portal2)
	{
		conn->portal2->sound_conn = NULL;
	}
	else if(conn->portal1->neighbor) // make sure we clear it out
	{
		conn->portal1->neighbor->sound_conn = NULL;
	}

	MP_FREE(SoundSpaceConnector, conn);	

	space_state.needs_rebuild = 1;
}

F32 sndConnCalcIntersection(const SoundSpaceConnector *conn, const Vec3 start, const Vec3 end, Vec3 isect)
{
	return boxLineNearestPoint(conn->local_min, conn->local_max, conn->world_mat, NULL, start, end, isect);
}

SoundSpace* sndSpaceConnectorOtherSpace(SoundSpaceConnector *soundSpaceConnector, SoundSpace *space)
{
	return soundSpaceConnector->space1==space ? soundSpaceConnector->space2 : soundSpaceConnector->space1;
}

bool sndSpaceConnectorConnectsToSpace(SoundSpaceConnector *soundSpaceConnector, SoundSpace *space)
{
	return soundSpaceConnector->space1 == space || soundSpaceConnector->space2 == space;
}

void sndSpaceConnectorConnectSpaces(SoundSpaceConnector *soundSpaceConnector, SoundSpace *space1, SoundSpace *space2)
{
	// TODO(gt): should this executed here or when connector is created?
	centerVec3(soundSpaceConnector->world_min, soundSpaceConnector->world_max, soundSpaceConnector->world_mid);

	soundSpaceConnector->space1 = space1;
	soundSpaceConnector->space2 = space2;

	sndSpaceAddConnector(soundSpaceConnector->space1, soundSpaceConnector);
	sndSpaceAddConnector(soundSpaceConnector->space2, soundSpaceConnector);
}


void sndSpaceConnectorFindSpaces(SoundSpaceConnector *soundSpaceConnector)
{
	SoundSpace *space1 = NULL, *space2 = NULL;
	
	// use the connector's portal to get the owner Room of the connection
	if(soundSpaceConnector->portal1->parent_room)
	{
		// extract the spaces from the Room;
		space1 = soundSpaceConnector->portal1->parent_room->sound_space;

		if(soundSpaceConnector->portal2)
		{
			space2 = soundSpaceConnector->portal2->parent_room->sound_space;
		}

		// ********************************************************************
		// 9.18.09 GT
		// Disabled single direction portals -- must overlap with another portal to be active!
		// ********************************************************************
		//
		//if(!space1)
		//{
		//	space1 = space_state.null_space;
		//}

		//if(!space2)
		//{
		//	space2 = space_state.null_space;
		//}

		if(space1 == space2)
		{
			// TODO(AM): Error... something is wrong
			//devassertmsg(0, "Two conns connecting to null space... Bug Adam");
		}
		else if(space1 && space2)
		{
			sndSpaceConnectorConnectSpaces(soundSpaceConnector, space1, space2);
			//sndSpaceConnectConnector(space1, space2, conn);
		}

//		space_state.needs_reconnect = 1;
	}
}

void sndSpaceConnectorReleaseEdges(SoundSpaceConnector *soundSpaceConnector)
{
	int i;
	int numEdges = eaSize(&soundSpaceConnector->connectorEdges);

	for(i = 0; i < numEdges; i++)
	{
		free(soundSpaceConnector->connectorEdges[i]);
	}
	eaClear(&soundSpaceConnector->connectorEdges);
}


void sndSpaceConnectorFindConnectorsInSpace(SoundSpaceConnector *soundSpaceConnector, SoundSpace *soundSpace)
{
	int i, numSpaces;
	SoundSpace *otherSpace = sndConnGetOther(soundSpaceConnector, soundSpace);

	numSpaces = eaSize(&soundSpace->connectors);
	for(i = 0; i < numSpaces; i++)
	{
		SoundSpaceConnector *soundSpaceConnector2 = soundSpace->connectors[i];

		// make sure they're not the same
		if(soundSpaceConnector2 != soundSpaceConnector)
		{
			// make sure they connect to different rooms
			if( sndConnGetOther(soundSpaceConnector2, soundSpace) != otherSpace ) 
			{
				SoundSpaceConnectorEdge *connectorEdge;
				F32 dist;

				// both connectors are in the same room and each connect to different rooms, so measure distance
				
				dist = distance3(soundSpaceConnector->world_mid, soundSpaceConnector2->world_mid);
				connectorEdge = callocStruct(SoundSpaceConnectorEdge);

				connectorEdge->node1 = soundSpaceConnector;
				connectorEdge->node2 = soundSpaceConnector2;
				connectorEdge->cost = dist;

				eaPush(&soundSpaceConnector->connectorEdges, connectorEdge);
			}
		}
	}
}

SoundSpaceConnector *sndSpaceConnectorEdgeOtherConnector(SoundSpaceConnectorEdge *soundSpaceConnectorEdge, SoundSpaceConnector *soundSpaceConnector)
{
	return soundSpaceConnectorEdge->node1 == soundSpaceConnector ? soundSpaceConnectorEdge->node2 : soundSpaceConnectorEdge->node1;
}

int sndSpaceConnectorSortByCost(const SoundSpaceConnector **s1, const SoundSpaceConnector **s2)
{
	return SIGN((*s1)->cost - (*s2)->cost);
}

int sndSpaceConnectorEdgesSortByCost(const SoundSpaceConnectorEdge **s1, const SoundSpaceConnectorEdge **s2)
{
	return SIGN((*s1)->cost - (*s2)->cost);
}

#endif

AUTO_RUN;
void sndConnRegisterMsgHandler(void)
{
#ifndef STUB_SOUNDLIB
	sndObjectRegisterClass(SSC_CLASS_NAME, sndConnObjectMsgHandler);
#endif
}
