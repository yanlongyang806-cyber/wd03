#ifndef _GSLWORLDDEBUG_
#define _GSLWORLDDEBUG_

typedef U32 EntityRef;
typedef struct Entity Entity;

// This keeps track of all the entities requesting beacon debug info
// It will try and send all beacons and connections to the client
// but at a limited rate so the server never stalls
void beaconDebugOncePerFrame(F32 timeElapsed);

#endif