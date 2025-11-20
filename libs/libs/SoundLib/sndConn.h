/***************************************************************************



***************************************************************************/


/* This file contains the public interface to the sound library */

#pragma once

#ifndef _SNDCONN_H
#define _SNDCONN_H
GCC_SYSTEM

#include "stdtypes.h"
#include "sndObject.h"

typedef struct SoundObjectMsg SoundObjectMsg;
typedef struct DebuggerObject DebuggerObject;

typedef struct RoomPortal RoomPortal;

typedef enum SoundSpaceConnectorType {
	SSCT_DOOR,
	SSCT_THINWALL,
} SoundSpaceConnectorType;

typedef struct SoundSpaceConnector SoundSpaceConnector;

typedef struct SoundSpaceConnectorTransmission {
	SoundSpaceConnector *conn;
	F32 audibility;
} SoundSpaceConnectorTransmission;

typedef struct SoundSpaceConnectorProperties {
	F32 attn_0db_range;						// Distance over which volume is not dropped
	F32 attn_max_range;						// Maximum distance ( x > this -> 0 volume)

	SoundSpaceConnectorTransmission **audibleConns; // Connections I can possibly hear
} SoundSpaceConnectorProperties;

typedef struct SoundSpaceConnectorPathNode {
	SoundSpaceConnector		*conn;	// Connection to current space
	Vec3					pos;
	F32						base_cost;		// Just based off distance from conn
	F32						real_cost;		// Entire distance to listener
} SoundSpaceConnectorPathNode;


typedef struct SoundSpaceConnectorEdge {
	SoundSpaceConnector *node1;
	SoundSpaceConnector *node2;
	F32 cost;
} SoundSpaceConnectorEdge;

typedef struct SoundSpaceConnector {
	SoundObject obj;
	
	SoundSpaceConnectorEdge **connectorEdges;

	SoundSpace *space1;
	SoundSpace *space2;

	RoomPortal *portal1;
	RoomPortal *portal2;

	SoundSpaceConnectorProperties props1;	// For space2->space1
	SoundSpaceConnectorProperties props2;	// Space1->space2

	Vec3 world_min;							// Data for "moving" sounds between spaces
	Vec3 world_max;							// Also used for raycasting at connectors, but not yet
	Vec3 world_mid;							// Also used to determine space1 and space2 (x -> y -> z)
	Vec3 local_min;
	Vec3 local_max;
	Mat4 world_mat;

	F32 audibility;							// Current audibility

	SoundSource **transmitted;				// Sources transmitted through this connector

	SoundSpaceConnectorType type;
	REF_TO(DebuggerObject) debug_object;

	void *active_props_dsp;
	REF_TO(SoundDSP) dsp_ref;

	SoundSpaceConnector *previousConnector;	// Temporary value for calculating shortest path
	F32 cost;								// Temporary value for calculating shortest path
	U32				visited		: 1;		// Temporary value for calculating shortest path

	U32				is_dsp		: 1;		// Tells me if I have a non-mixer DSP
	U32				to_1		: 1;		// to_1 means 2->1, !to_1 means 1->2
	//U32				connected	: 1;
	U32				destroyed	: 1;
	U32				was_audible	: 1;
} SoundSpaceConnector;

SoundSpaceConnector* sndConnCreate(RoomPortal *portal);
U32 sndConnObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg);
void sndConnDestroy(RoomPortal *portal);
void sndConnFree(SoundSpaceConnector *conn);

SoundSpace* sndConnGetOther(SoundSpaceConnector *conn, SoundSpace *space);
SoundSpace* sndConnGetSource(SoundSpaceConnector *conn);
SoundSpace* sndConnGetTarget(SoundSpaceConnector *conn);
U32 sndConnIsSpaceSource(SoundSpaceConnector *conn, SoundSpace *source);
U32 sndConnIsSpaceTarget(SoundSpaceConnector *conn, SoundSpace *source);
void sndConnSetTarget(SoundSpaceConnector *conn, SoundSpace *space);
void sndConnSetSource(SoundSpaceConnector *conn, SoundSpace *space);
SoundSpaceConnectorProperties* sndConnGetSourceProps(SoundSpaceConnector *conn);
SoundSpaceConnectorProperties* sndConnGetTargetProps(SoundSpaceConnector *conn);
SoundSpaceConnectorProperties* sndConnGetSpaceProperties(SoundSpaceConnector *conn, SoundSpace *space);
SoundSpaceConnectorProperties* sndConnGetOtherProps(SoundSpaceConnector *conn, SoundSpace *space);
SoundSpaceConnectorProperties* sndConnGetProps(SoundSpaceConnector *conn, SoundSpace *space);
F32 sndConnGetAudibility(SoundSpaceConnectorProperties *props, F32 dist);
U32 sndConnGetWhichSpace(SoundSpaceConnector *conn, SoundSpace *space);
U32 sndConnConnectsSpace(SoundSpaceConnector *conn, SoundSpace *space);

//F32 sndConnPointDist(SoundSpaceConnector *conn, const Vec3 pt, Vec3 coll);
F32 sndConnLineDist(const SoundSpaceConnector *conn, const Vec3 s, const Vec3 e, Vec3 coll);
F32 sndConnPointDist(const SoundSpaceConnector *conn, const Vec3 p, Vec3 coll);
F32 sndConnConnDist(SoundSpaceConnector *conn1, SoundSpaceConnector *conn2);
F32 sndConnSimplePointDist(const SoundSpaceConnector *conn, const Vec3 pt);
F32 sndConnCalcIntersection(const SoundSpaceConnector *conn, const Vec3 start, const Vec3 end, Vec3 isect);

void sndConnTransmissionCreate(SoundSpaceConnector *source, SoundSpaceConnector *target, SoundSpace *space, F32 dist);

// get connector's other space
SoundSpace* sndSpaceConnectorOtherSpace(SoundSpaceConnector *soundSpaceConnector, SoundSpace *space);

// does the connector connect to space
bool sndSpaceConnectorConnectsToSpace(SoundSpaceConnector *soundSpaceConnector, SoundSpace *space);

// release all connector edges
void sndSpaceConnectorReleaseEdges(SoundSpaceConnector *soundSpaceConnector);

// calculate distances between the connector and any other connectors in the space that attach to a different adjacent space
void sndSpaceConnectorFindConnectorsInSpace(SoundSpaceConnector *soundSpaceConnector, SoundSpace *soundSpace);

// find and set the two spaces that are connected via the SoundSpaceConnector
void sndSpaceConnectorFindSpaces(SoundSpaceConnector *soundSpaceConnector);

// get the other connector from the edge
SoundSpaceConnector *sndSpaceConnectorEdgeOtherConnector(SoundSpaceConnectorEdge *soundSpaceConnectorEdge, SoundSpaceConnector *soundSpaceConnector);

// sort connector by cost
int sndSpaceConnectorSortByCost(const SoundSpaceConnector **s1, const SoundSpaceConnector **s2);

// sort edges by cost
int sndSpaceConnectorEdgesSortByCost(const SoundSpaceConnectorEdge **s1, const SoundSpaceConnectorEdge **s2);

#endif
