#pragma once
GCC_SYSTEM

typedef struct Packet Packet;

typedef void (*MapNameRecordCallback)(const char* mapname);

void worldReceiveUpdate(SA_PARAM_NN_VALID Packet *pak, SA_PARAM_OP_VALID MapNameRecordCallback nameCB);
void worldReceivePeriodicUpdate(SA_PARAM_NN_VALID Packet *pak);
void worldReceiveLockedUpdate(Packet *pak, NetLink *link);
int worldGetUndosRemaining(void);
int worldGetRedosRemaining(void);

