#ifndef _WORLDGRIDSEND_H_
#define _WORLDGRIDSEND_H_
GCC_SYSTEM

typedef struct Packet Packet;
typedef struct NetLink NetLink;
typedef struct ResourceCache ResourceCache;

int worldNeedsUpdate(U32 last_update_time);
bool worldSendUpdate(SA_PARAM_NN_VALID Packet *pak, int full_update, SA_PARAM_OP_VALID ResourceCache *ref_cache_if_allow_updates, SA_PARAM_NN_VALID U32 *last_update_time);
bool worldSendPeriodicUpdate(SA_PARAM_NN_VALID Packet *pak, SA_PARAM_NN_VALID U32 *last_sky_update_time, bool bRecordSkyTime);
void worldPauseLockedUpdates(bool pause);
void worldSendLockedUpdate(SA_PARAM_NN_VALID NetLink *link, int cmd);

#endif//_WORLDGRIDSEND_H_
