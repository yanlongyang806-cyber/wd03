#pragma once

typedef struct TrackedMachineState TrackedMachineState;
typedef struct Packet Packet;
typedef struct NetLink NetLink;

extern bool gbDoDynHoggPruning;
void AddNamespaceForDynHoggPruning(TrackedMachineState *pMachine, char *pNamespace);
void DynHoggPruning_Update(void);

bool DynHoggPruningActiveForMachine(TrackedMachineState *pMachine);

void HandleDynHogPruningComment(Packet *pPak, NetLink *pLink);
void HandleDynHogPruningSuccess(Packet *pPak, NetLink *pLink);
void HandleDynHogPruningFailure(Packet *pPak, NetLink *pLink);
