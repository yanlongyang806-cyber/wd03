/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

typedef struct StashTableImp *StashTable;




typedef struct NameList NameList;

typedef const char* NameList_GetNextCallback(NameList *pList, bool skipDupes);
typedef void NameList_ResetCallback(NameList *pList);
typedef void NameList_FreeCallback(NameList *pList);
typedef bool NameList_HasEarlierDupeCallback(NameList *pList, const char* name);

typedef const char* NameList_GetNextDataCallback(NameList *pList, void *pData);
typedef void NameList_DataCallback(NameList *pList, void *pData);

typedef struct NameList
{
	NameList_GetNextCallback *pGetNextCB;
	NameList_HasEarlierDupeCallback *pHasEarlierDupeCB;
	NameList_ResetCallback *pResetCB;
	NameList_FreeCallback *pFreeCB;
	char *pGlobalName; //only set if NameList_AssignName is called on this namelist
	U32 erClientEntity;	//the client EntityRef, only set in RequestAutoCompletionArgNamesFromServer
	bool bUpdateClientDataPerRequest; // allow the client to request new data from the server for each query
} NameList;


void FreeNameList(NameList *pNameList);

bool NameList_HasEarlierDupe(NameList *pList, const char* name);

//goes through a namelist and finds all strings that contain all the substrings which are separated in the 
//given string by slashes. For instance "dog/foo" returns all strings containing "dog" and "foo"
void NameList_FindAllMatchingStrings(NameList *pList, const char *pInString, const char ***pppOutStrings);
void NameList_FindAllPrefixMatchingStrings(NameList *pList, const char *pInStringPrefix, const char ***pppOutStrings);

//assigns a string name to a name list via a global stash table, to make it easy for others to find 
//this list
void NameList_AssignName(NameList *pList, char *pName);

//find a name list by name
NameList *NameList_FindByName(char *pName);

//set a name and a callback. If someone tries to call NameList_FindByName, call the callback which
//will create a namelist. (This allows a namelist that is regenereated differently each time it is accessed)
typedef NameList *NameList_GetNamedListCB(void);
void NameList_RegisterGetListCallback(char *pName, NameList_GetNamedListCB *pCB);

//a list of all the commands in a CmdList
typedef struct CmdList CmdList;
NameList *CreateNameList_CmdList(CmdList *pCmdList, int iMaxAccessLevel);
void NameList_CmdList_SetAccessLevel(NameList *pNameList, int iAccessLevel);

// A list of commands that have been hand picked for use in chat autocomplete
typedef struct ChatAutoCompleteEntryListList ChatAutoCompleteEntryListList;
NameList *CreateNameList_ChatCmdList(ChatAutoCompleteEntryListList *pListList);

//a bucket of names which can be arbitrarily added to
NameList *CreateNameList_Bucket(void);
void NameList_Bucket_AddName(NameList *pList, const char *pName);
void NameList_Bucket_RemoveName(NameList *pList, const char *pName);
void NameList_Bucket_Clear(NameList *pList);

//a namelist that looks at a list of structures with a name
typedef void** (*NameList_StructArrayFunc)(void);
NameList* CreateNameList_StructArray(ParseTable *pti, const char* name, NameList_StructArrayFunc func);

//a bucket with access levels associated with the names
//This is useful for storing a list of remote commands that
//need to be iterated over taking into account access level.
NameList *CreateNameList_AccessLevelBucket(int iMaxAccessLevel);
void NameList_AccessLevelBucket_AddName(NameList *pList, const char *pName, int iAccessLevel);
void NameList_AccessLevelBucket_SetAccessLevel(NameList *pNameList, int iAccessLevel);
int NameList_AccessLevelBucket_GetAccessLevel(NameList *pNameList, const char *pName);

//a name list that uses custom GetNext, Reset, and Free callbacks
NameList *CreateNameList_Callbacks(NameList_GetNextDataCallback *pGetNextCB, NameList_DataCallback *pResetCB, NameList_DataCallback *pFreeCB, void *pData);

//a collection of other name lists
NameList *CreateNameList_MultiList(void);
void NameList_MultiList_AddList(NameList *pMultiList, NameList *pChildList);

//the names of things in a reference dictionary
NameList *CreateNameList_RefDictionary(char *pDictionaryName);

//names of things in a resource dictionary
NameList *CreateNameList_ResourceDictionary(char *pDictionaryName);

//names of available resource infos
NameList *CreateNameList_ResourceInfo(char *pDictionaryName);

//the names of things in a stash table (with string keys)
NameList *CreateNameList_StashTable(StashTable table);

//the names of things in a StaticDefine
NameList *CreateNameList_StaticDefine(StaticDefine *pDefine);

//the names of things in a cached MRUList
NameList *CreateNameList_MRUList(const char *name, int maxEntries, int maxStringSize);
void NameList_MRUList_AddName(NameList *pMRUList, const char *string);

//short filenames (no dirname or extension) of all files in a directory, optionally matching a
//name
NameList *CreateNameList_FilesInDirectory(const char *pDirName, const char *pNameToMatch);

//"name list types" used to generate name lists at run time
typedef enum
{
	NAMELISTTYPE_NONE,
	NAMELISTTYPE_PREEXISTING,
	NAMELISTTYPE_REFDICTIONARY,
	NAMELISTTYPE_RESOURCEDICTIONARY,
	NAMELISTTYPE_RESOURCEINFO,
	NAMELISTTYPE_STASHTABLE,
	NAMELISTTYPE_COMMANDLIST,
	NAMELISTTYPE_STATICDEFINE,
	NAMELISTTYPE_NAMED,
} enumNameListType;

NameList *CreateTempNameListFromTypeAndData(enumNameListType eType, void **pData);
