#pragma once
GCC_SYSTEM

typedef struct GroupTracker GroupTracker;
typedef struct SimpleGroupDef SimpleGroupDef;

// these return the number of bytes written
int saveGroupFileAs(SA_PARAM_OP_VALID LibFileLoad *lib, SA_PARAM_NN_VALID GroupDefLib *def_lib, SA_PARAM_NN_VALID const char *filename);
#define groupFixupBeforeWrite(def, clear_invalid) groupFixupBeforeWriteEx(def, clear_invalid, clear_invalid)
void groupFixupBeforeWriteEx(GroupDef *def, bool clear_invalid, bool silent);

// for ObjectLibrary.c
int worldSaveSimpleDefs(SA_PARAM_NN_STR const char *filename, SimpleGroupDef **defs);

