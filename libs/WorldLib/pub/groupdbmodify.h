#pragma once
GCC_SYSTEM

typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct GroupDefLib GroupDefLib;
typedef struct TrackerHandle TrackerHandle;
typedef struct JournalEntry JournalEntry;
typedef struct StashTableImp* StashTable;
typedef struct GroupChildSimpleData GroupChildSimpleData;

void groupdbTransactionBegin(void);
JournalEntry *groupdbTransactionEnd(void);
void groupdbUndo(JournalEntry *entry);

void groupdbUpdateBounds(SA_PARAM_NN_VALID GroupDefLib *def_lib);
void groupdbDirtyDef(SA_PARAM_NN_VALID GroupDef *def, int child_idx);
void groupdbDirtyTracker(SA_PARAM_NN_VALID GroupTracker *tracker, int child_idx);
void groupdbCheckRemoveDefFromLib(GroupDef *def, StashTable new_defs);

bool groupdbLoopCheck(GroupDef *parent, GroupDef *child);
void groupdbMove(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_NN_VALID Mat4 mat);
void groupdbSetScale(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 scale);
int groupdbTransfer(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_NN_STR const char *destFileName, SA_PARAM_OP_STR const char *newName);
SA_RET_OP_VALID TrackerHandle *groupdbInsertEx(SA_PARAM_NN_VALID const TrackerHandle *parent, SA_PARAM_NN_VALID GroupDef *def, Mat4 worldMat, F32 scale, const GroupChildSimpleData* pSimpleData, U32 seed, int index);
SA_RET_OP_VALID TrackerHandle *groupdbInsert(SA_PARAM_NN_VALID const TrackerHandle *parent, SA_PARAM_NN_VALID const TrackerHandle *child, int index, bool randomizeSeed);
SA_RET_OP_VALID TrackerHandle *groupdbCreate(SA_PARAM_NN_VALID const TrackerHandle *parent, int uid, SA_PARAM_NN_VALID Mat4 mat, F32 scale, int index);
void groupdbReplace(SA_PARAM_NN_VALID const TrackerHandle *handle, int uid);
void groupdbMoveToIndex(SA_PARAM_NN_VALID const TrackerHandle *handle, int newIdxInParent);
void groupdbDelete(SA_PARAM_NN_VALID const TrackerHandle *handle);
void groupdbDeleteList(const TrackerHandle **handles);
void groupdbDestroy(int uid);
void groupdbInstance(SA_PARAM_NN_VALID const TrackerHandle *handle, bool recurse);
void groupdbRename(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_NN_STR const char *newName);
void groupdbSetTags(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_NN_STR const char *newTags);
void groupdbRandomize(SA_PARAM_NN_VALID const TrackerHandle *handle);
void groupdbEditProperty(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_NN_STR const char *propName, SA_PARAM_OP_STR const char *propVal);

// scope name editing
void groupdbSetUniqueScopeName(SA_PARAM_NN_VALID GroupDef *scopeDef, SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_OP_STR const char *groupName, SA_PARAM_NN_STR const char *name, bool update);
void groupdbDeleteScopeEntry(SA_PARAM_NN_VALID const TrackerHandle *handle);

// Editor override values
void groupClearOverrideParameters( void );
void groupSetOverrideIntParameter( const char* parameterName, int value );
void groupSetOverrideStringParameter( const char* parameterName, const char *value );
