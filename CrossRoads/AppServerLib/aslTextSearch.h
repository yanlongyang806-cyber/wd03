#pragma once

//register a bunch of pieces of text, each with an ID. Then do nice google-style searches of some sort and get
//back IDs that match. Currently using SQLite internally, might switch to LUCENE at some point

typedef struct AslTextSearchManager AslTextSearchManager;

AslTextSearchManager *aslTextSearch_Init(const char *pName);

//return false on failure
//
//bStringIsEffectivelyStatic can be true as long as you removeString before freeing/changing the string. Use this
//for the common case of a description string hanging off a struct
bool aslTextSearch_AddString(AslTextSearchManager *pManager, U64 iID, const char *pString, bool bStringIsEffectivelyStatic);
bool aslTextSearch_RemoveString(AslTextSearchManager *pManager, U64 iID);

//as the search occurs, it repeatedly calls this callback every time it finds a string match. If it
//gets true back from the callback, it continues. Otherwise it stops.
typedef bool (*aslTextSearchCB)(U64 iFoundID, void *pUserData);

//returns true if the search ran successfuly, even if nothing was returned
bool aslTextSearch_Search(AslTextSearchManager *pManager, const char *pString, aslTextSearchCB pCB, void *pUserData);

void aslTextSearch_Cleanup(AslTextSearchManager *pManager);