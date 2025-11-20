#pragma once
GCC_SYSTEM
//
// Prefs.h
//



// ---- General purpose preferences access -----------------------------------------------

void PrefSetOncePerFrame(void);

// Create a preference set.  Returns existing one if called again with the same file.
int PrefSetGet(const char *pcFileName);

// Get/set operations on preferences
const char *PrefGetString(int iPrefSet, const char *pcPrefName, const char *pcDefault);
int PrefGetInt(int iPrefSet, const char *pcPrefName, int iDefault);
F32 PrefGetFloat(int iPrefSet, const char *pcPrefName, F32 fDefault);
int PrefGetPosition(int iPrefSet, const char *pcPrefName, F32 *pX, F32 *pY, F32 *pW, F32 *pH);

void PrefStoreString(int iPrefSet, const char *pcPrefName, const char *pcValue);
void PrefStoreInt(int iPrefSet, const char *pcPrefName, int iValue);
void PrefStoreFloat(int iPrefSet, const char *pcPrefName, F32 fValue);
void PrefStorePosition(int iPrefSet, const char *pcPrefName, F32 x, F32 y, F32 w, F32 h);

// Text parser operations
// The get operation modifies the provided struct if the preference has a value
// Does support all struct types, just does toSimple and fromSimple, no recursion, but is forward/backward compatible
// Returns 0 if any field was not read (e.g. a new field was added since last written)
int PrefGetStruct(int iPrefSet, const char *pcPrefName, ParseTable *pParseTable, void *pStruct);
void PrefStoreStruct(int iPrefSet, const char *pcPrefName, ParseTable *pParseTable, void *pStruct);

// General use operations
bool PrefIsSet(int iPrefSet, const char *pcPrefName);
void PrefClear(int iPrefSet, const char *pcPrefName);


// ---- Game preferences access ---------------------------------------------------------

// These variations all act on the game preference set
// They initialize that preference set if required

// Get the Id of the game prefs
int GamePrefGetPrefSet(void);

// Get/set operations on preferences
const char *GamePrefGetString(const char *pcPrefName, const char *pcDefault);
int GamePrefGetInt(const char *pcPrefName, int iDefault);
F32 GamePrefGetFloat(const char *pcPrefName, F32 fDefault);
int GamePrefGetPosition(const char *pcPrefName, F32 *pX, F32 *pY, F32 *pW, F32 *pH);

void GamePrefStoreString(const char *pcPrefName, const char *pcValue);
void GamePrefStoreInt(const char *pcPrefName, int iValue);
void GamePrefStoreFloat(const char *pcPrefName, F32 fValue);
void GamePrefStorePosition(const char *pcPrefName, F32 x, F32 y, F32 w, F32 h);

// Text parser get/set operations
// The get operation modifies the provided struct if the preference has a value
// Does support all struct types, just does toSimple and fromSimple, no recursion, but is forward/backward compatible
// Returns 0 if any field was not read (e.g. a new field was added since last written)
int GamePrefGetStruct(const char *pcPrefName, ParseTable *pParseTable, void *pStruct);
void GamePrefStoreStruct(const char *pcPrefName, ParseTable *pParseTable, void *pStruct);

// General use operations
bool GamePrefIsSet(const char *pcPrefName);
void GamePrefClear(const char *pcPrefName);


// NOTE: Editor-specific preferences support is in "EditorPrefs.h"