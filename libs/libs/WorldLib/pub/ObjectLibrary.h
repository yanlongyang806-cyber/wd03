#ifndef _OBJECTLIBRARY_H_
#define _OBJECTLIBRARY_H_
GCC_SYSTEM

#include "stdtypes.h"

typedef struct GroupDef GroupDef;
typedef struct ModelHeader ModelHeader;
typedef struct ResourceGroup ResourceGroup;
typedef struct ResourceInfo ResourceInfo;
typedef struct GroupDefRef GroupDefRef;
typedef struct GroupDefLib GroupDefLib;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct GroupDefList
{
	int							version; AST(NAME("Version"))
	GroupDef					**defs; AST(NAME("Def"))
} GroupDefList;
extern ParseTable parse_GroupDefList[];
#define TYPE_parse_GroupDefList GroupDefList

#define OBJLIB_EXTENSION ".objlib"
#define MODELNAMES_EXTENSION ".modelnames"
#define ROOTMODS_EXTENSION ".rootmods"

AUTO_STRUCT;
typedef struct GroupDefLockedFile
{
	const char *filename;		AST(FILENAME POOL_STRING)
} GroupDefLockedFile;

typedef void (*ObjectLibraryChangedCallback)(void);

ResourceGroup *objectLibraryGetRoot(void);
ResourceGroup *objectLibraryRefreshRoot(void);

#define OBJECT_LIBRARY_DICT "ObjectLibrary"

void objectLibraryLoad();

int objectLibraryLastUpdated(void);

bool objectLibraryLoaded(void);
bool objectLibraryInited(void);

void objectLibraryOncePerFrame(void);

// library piece names/model names
int objectLibraryUIDFromObjName(SA_PARAM_NN_STR const char *obj_name);
ResourceInfo *objectLibraryGetResource(SA_PARAM_NN_VALID const GroupDefRef *def_ref);

GroupDef *objectLibraryGetDummyGroupDef(void);
GroupDef *objectLibraryGetGroupDef(int obj_uid, bool editing_copy);
GroupDef *objectLibraryGetGroupDefByName(const char *obj_name, bool editing_copy);
GroupDef *objectLibraryGetGroupDefFromResource(const ResourceInfo *info, bool editing_copy);
GroupDef *objectLibraryGetGroupDefFromRef(const GroupDefRef *def_ref, bool editing_copy);

void objectLibraryAddEditingCopy(GroupDef *def);
GroupDef *objectLibraryGetEditingCopy(GroupDef *def, bool create, bool overwrite);
GroupDef *objectLibraryGetEditingGroupDef(int obj_uid, bool allow_temporary_defs);
GroupDefLib *objectLibraryGetEditingDefLib(void);

void objectLibraryGetWritePath(const char *in_path, char *out_path, size_t out_path_size);
bool objectLibraryGroupEditable(GroupDef *def);
bool objectLibraryGroupSetEditable(GroupDef *def);
bool objectLibrarySetFileEditable(const char *filename);

GroupDefLib *objectLibraryGetDefLib(void);

char *objectLibraryPathFromObj(int obj_uid);
char *objectLibraryPathFromObjName(SA_PARAM_NN_STR const char *obj_name);

void objectLibraryFreeModelsAndEditingLib(void);

// Modifies the tags on an object library piece, and saves it out. This will cause it to get reloaded
bool objectLibraryChangeTags(const char *objName, const char *newTags);

const char *objectLibraryGetFilePath(GroupDefLockedFile *file);
void objectLibraryGetEditableFiles(GroupDefLockedFile ***file_list);

bool objectLibraryGetUnsaved(void);
bool objectLibrarySave(const char *filename);

void objectLibraryDebugCheck();

void reloadObjectLibraryFile(const char *relpath);
void objectLibraryDoneReloading(void);

bool objectLibraryConsistencyCheck(bool do_assert);

//////////////////////////////////////////////////////////////////////////
// for GetVrml:

typedef struct SimpleGroupDef SimpleGroupDef;

typedef struct SimpleGroupChild
{
	Mat4 mat;
	U32 seed;
	SimpleGroupDef *def;
} SimpleGroupChild;

typedef struct SimpleGroupDef
{
	char name_str[1024];
	int name_uid;
	SimpleGroupChild **children;

	// leaf data
	char modelname[1024];
	
	void *user_data;

	bool fixedup;
	bool is_root;

} SimpleGroupDef;

int objectLibraryWriteModelnamesToFile(SA_PARAM_NN_STR const char *fname, SimpleGroupDef **groups, bool no_checkout, bool is_core); // returns 0 if unable to write file
#endif //_OBJECTLIBRARY_H_
