#pragma once
typedef struct DumpData DumpData;

U32 getNewDumpID(DumpData *pData);
DumpData * findDumpData(int id);
int countReceivedDumps(U32 id, U32 dumpflags);
int getIncomingDumpCount(U32 id);
void addIncomingDumpCount(U32 id);
void removeIncomingDumpCount(U32 id);