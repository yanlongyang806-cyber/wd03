/***************************************************************************



***************************************************************************/

//a "name table" is a simple wrapper around a stash table which stores strings
//with string names, and provides some nice simple sending and receiving functionality
//
//It always duplicates all strings passed into it either as keys or data.

#pragma once
GCC_SYSTEM

typedef struct StashTableImp *StashTable;
typedef StashTable NameTable;
typedef struct Packet Packet;

//if pPack is non-NULL, then initialize the name table with the pairs found in the packet
NameTable CreateNameTable(Packet *pPack);
void DestroyNameTable(NameTable table);

//returns NULL if lookup fails
void *NameTableLookup(NameTable table, const char *pKey, int *piSize);

//automatically replaces existing, if any
void NameTableAddBytes(NameTable table, const char *pKey, void *pData, int iSize);

__forceinline void NameTableAddString(NameTable table, const char *pKey, char *pString) { NameTableAddBytes(table, pKey, pString, (int)(strlen(pString)) + 1); }

//puts the contents of the name table into a packet. If pKeyList is non-NULL, then pKeyList is
//a space-separated list of keys, and only those keys (if they exist) should be put into the packet
void NameTablePutIntoPacket(NameTable table, Packet *pPack, char *pKeyList);

void DumpNameTableIntoEString(NameTable table, char **ppEString);
