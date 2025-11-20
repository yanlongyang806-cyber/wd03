#include "ObjectLibrary.h"
#include <string.h>
#include "gimmeDLLWrapper.h"
#include "StringCache.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "FolderCache.h"
#include "timing.h"
#include "winutil.h"
#include "StringUtil.h"
#include "logging.h"
#include "ResourceSystem_Internal.h"

#include "RoomConn.h" // For debug dump
#include "wlState.h"
#include "wlAutoLOD.h" // For debug dump
#include "wlEncounter.h" // For debug dump
#include "wlModelReload.h"
#include "wlUGC.h"
#include "WorldGridPrivate.h"
#include "WorldGridLoadPrivate.h"

#include "WorldGridLoadPrivate_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

typedef struct ObjectLibrary
{
	bool			bLoaded;
	bool			bInited;
	U32				iLastUpdated;
	int				iDictRefs;

	// The Def Lib which points directly into the reference dictionary
	GroupDefLib		sObjectLibraryLib;

	// The Def Lib which stores the editing copies
	GroupDefLib		sObjectEditingLib;
	// List of defs which have already been imported into editing_lib (to avoid importing deleted defs)
	StashTable		stObjectEditingImports; 
	// List of files we have locked
	StashTable		stLockedFiles;

	GroupDef *		pDummyGroup; // uid = -1

	ResourceGroup *	pResourceGroupRoot;

	bool			bNeedsReload;

} ObjectLibrary;

static DictionaryHandle g_ObjectLibraryDictionary;
static ObjectLibrary g_ObjectLibrary;

int gbObjectLibraryDebugEditingCopies = 0;
AUTO_CMD_INT(gbObjectLibraryDebugEditingCopies, ObjectLibraryDebugEditingCopies);

void objectLibraryRefreshRootsIfExist(void);
void objectLibraryHandleEvent(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData);

//////////////////////////////////////////////////////////////////////////
// for GetVrml:

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct GUIDEntry
{
	char *modelname;	AST( STRUCTPARAM )
	int uid;			AST( STRUCTPARAM )
} GUIDEntry;

void resDictMaintainResourceIDs(DictionaryHandleOrName dictHandle, bool maintainIDs);
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct GlobalUIDs
{
	GUIDEntry **entries; AST( NAME(GUID) )
	StashTable entry_hash; NO_AST
} GlobalUIDs;

#include "AutoGen/ObjectLibrary_h_ast.c"
#include "AutoGen/ObjectLibrary_c_ast.c"

//////////////////////////////////////////////////////////////////////////
// GroupDef validation

static int objectLibraryValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GroupDef *group_def, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_CHECK_REFERENCES:
		if (isDevelopmentMode()) {
			validateGroupPropertyStructs(group_def, group_def->filename, group_def->name_str, group_def->name_uid, true);
			return VALIDATE_HANDLED;
		}
	}
	return VALIDATE_NOT_HANDLED;
}

//////////////////////////////////////////////////////////////////////////
// GroupDef fixup

static GroupDefList *g_RootModsList = NULL; // Used for fixup below
static bool g_LoadingRootMods = false;

static void objectLibraryFixupGroupTags(GroupDef *def)
{
	int i;
	const char **tags_list = NULL;
	char *out_tags = NULL;
	bool needs_validation = false;

	DivideString(def->tags, ",", (char***)&tags_list, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_ALLOCADD);
	
	for (i = 0; i < eaSize(&def->prop_load_deprecated); i++)
		if (!stricmp(def->prop_load_deprecated[i]->name, "tags"))
		{
			DivideString(def->prop_load_deprecated[i]->value, ",", (char***)&tags_list, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_ALLOCADD);
			StructDestroy(parse_PropertyLoad, def->prop_load_deprecated[i]);
			eaRemove(&def->prop_load_deprecated, i);
			break;
		}

	if (def->property_structs.genesis_challenge_properties != NULL)
	{
		eaPushUnique(&tags_list, allocAddString("genesischallenge"));
	}
	else
	{
		FOR_EACH_IN_EARRAY(tags_list, const char, tag)
		{
			if (stricmp(tag, "genesischallenge") == 0)
			{
				eaRemove(&tags_list, FOR_EACH_IDX(tags_list, tag));
			}
		}
		FOR_EACH_END;
	}

	estrCreate(&out_tags);
	FOR_EACH_IN_EARRAY_FORWARDS(tags_list, const char, tag)
	{
		if (FOR_EACH_IDX(tags_list, tag) > 0)
			estrAppend2(&out_tags, ",");
		estrAppend2(&out_tags, tag);

		if (stricmp(tag, "UGC") != 0)
			needs_validation = true;
	}
	FOR_EACH_END;

	if (out_tags[0])
		StructCopyString(&def->tags, out_tags);
	else
		StructFreeStringSafe(&def->tags);
	estrDestroy(&out_tags);
}


AUTO_FIXUPFUNC;
TextParserResult objectLibraryFixupDef(GroupDef *group_def, enumTextParserFixupType eType, void *pExtraData)
{
	if (!group_def || g_LoadingRootMods)
		return 1;
	switch (eType)
	{
		xcase FIXUPTYPE_CONSTRUCTOR:
		{
			group_def->version = GROUP_DEF_VERSION;
		}
		xcase FIXUPTYPE_POST_BIN_READ:
		{
			groupEnsureValidVersion(group_def);
		}
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			char *scope;
			char temp[MAX_PATH];
			GroupDef *found_rootmod = NULL;
			bool version_set = false;
			TextParserState *tps = (TextParserState*)pExtraData;

			if (group_def->filename)
			{
				strcpy(temp, group_def->filename);
				// Apply rootmods
				if (g_RootModsList && strEndsWith(group_def->filename, MODELNAMES_EXTENSION))
				{
					FOR_EACH_IN_EARRAY(g_RootModsList->defs, GroupDef, rootmod)
					{
						if (rootmod->name_uid == group_def->name_uid)
						{
							if (!found_rootmod)
							{
								// Apply rootmod to original def
								eaDestroyStruct(&rootmod->children, parse_GroupChild);
								StructApplyDefaults(parse_GroupDef, group_def, rootmod, 1, 0, false);
								group_def->bfParamsSpecified[0] |= rootmod->bfParamsSpecified[0];
								group_def->bfParamsSpecified[1] |= rootmod->bfParamsSpecified[1];
								group_def->bfParamsSpecified[2] |= rootmod->bfParamsSpecified[2];
								groupEnsureValidVersion(rootmod);
								group_def->version = rootmod->version;
								version_set = true;
								found_rootmod = rootmod;
							}
							else
							{
								char id_str[32];
								sprintf(id_str, "%d", group_def->name_uid);
								ErrorFilenameDup(rootmod->filename, found_rootmod->filename, id_str, "GroupDef override");
							}
						}
					}
					FOR_EACH_END;

					changeFileExt(group_def->filename, ROOTMODS_EXTENSION, temp);
					group_def->filename = allocAddString(temp);
					ParserBinAddFileDep(tps,group_def->filename);
				}

				// Fix up tags
				objectLibraryFixupGroupTags(group_def);

				// Set scope based on path
				getDirectoryName(temp);
				scope = strstri(temp, "object_library/");
				if (scope)
				{
					scope = scope + strlen("object_library/");
				}
				else
				{
					scope = temp;
				}
				group_def->scope = allocAddString(scope);
			}

			if(!version_set)
				groupEnsureValidVersion(group_def);

			groupFixupChildren(group_def);
		}
		xcase FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
		case FIXUPTYPE_POST_RELOAD:
		{
			groupFixupAfterRead(group_def, true); 

			if (stashFindPointer(g_ObjectLibrary.stLockedFiles, group_def->filename, NULL))
			{
				if (!objectLibraryGroupEditable(group_def))
				{
					//If the file is locked, but the def is not editable, then this is a new def created by structparser in reaction to GetVrml. This flag needs to be set to edit it.
					group_def->is_new = true;
				}
			}
		}
	}
	return 1;
}

// We're about to load a modelnames file. Attempt to load the rootmods file for it.
// If modelnames_file is NULL, load *all* rootmods.
static void objectLibraryPreLoadFile(const char *rootmods_file)
{
	assert(!g_RootModsList);
	g_RootModsList = StructCreate(parse_GroupDefList);
	g_LoadingRootMods = true;
	if (!rootmods_file)
	{
		loadstart_printf("Loading Object Library Rootmods...");
		ParserLoadFiles("object_library", ".rootmods", NULL, PARSER_OPTIONALFLAG, parse_GroupDefList, g_RootModsList);
		loadend_printf("done. (%d groups)", eaSize(&g_RootModsList->defs));
	}
	else
	{
		ParserLoadFiles(NULL, rootmods_file, NULL, PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING|PARSER_OPTIONALFLAG, parse_GroupDefList, g_RootModsList);
	}
	g_LoadingRootMods = false;
}

static void objectLibraryPostLoadFile()
{
	StructDestroySafe(parse_GroupDefList, &g_RootModsList);
}

//////////////////////////////////////////////////////////////////////////
// Register and load the dictionary

AUTO_RUN;
void RegisterObjectLibraryDict(void)
{
#ifndef _XBOX
	groupLibInit(&g_ObjectLibrary.sObjectLibraryLib);
	groupLibInit(&g_ObjectLibrary.sObjectEditingLib); 
	g_ObjectLibrary.sObjectEditingLib.editing_lib = true; 
	g_ObjectLibrary.stObjectEditingImports = stashTableCreateInt(1024);
	g_ObjectLibrary.stLockedFiles = stashTableCreateWithStringKeys(10, StashDefault);
#endif
	g_ObjectLibraryDictionary = RefSystem_RegisterSelfDefiningDictionary(OBJECT_LIBRARY_DICT, true, parse_GroupDef, true, false, NULL);
	if( IsGameServerSpecificallly_NotRelatedTypes() ) {
		resDictProvideMissingResources( g_ObjectLibraryDictionary );
		resDictProvideMissingRequiresEditMode( g_ObjectLibraryDictionary );
		resDictManageValidation( g_ObjectLibraryDictionary, objectLibraryValidateCB );
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_ObjectLibraryDictionary, NULL, ".Scope", ".Tags", ".Name", NULL);
			resDictMaintainResourceIDs(g_ObjectLibraryDictionary, true);
		}
	}
	else if( IsClient() ) {
		resDictRequestMissingResources( g_ObjectLibraryDictionary, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand );
	}

	resDictRegisterEventCallback(g_ObjectLibraryDictionary, objectLibraryHandleEvent, NULL);
	resDictSetParseName(g_ObjectLibraryDictionary, "Def");
	resDictSetUseAnyName(g_ObjectLibraryDictionary, true);
}

static void objectLibraryUpdateChildren(GroupDef *def, int id)
{
	FOR_EACH_IN_EARRAY(def->children, GroupChild, child)
	{
		GroupDef *child_def = groupChildGetDef(def, child, false);
		if (child_def && strEndsWith(child_def->name_str, "&"))
		{
			child_def->root_id = id;
			objectLibraryUpdateChildren(child_def, id);
		}
	}
	FOR_EACH_END;
}

void objectLibraryLoad()
{
	int file_idx;
	char **out_files;

	if (g_ObjectLibrary.bLoaded)
		return;

	out_files = fileScanDir(logGetDir());
	for (file_idx = 0; file_idx < eaSize(&out_files); file_idx++)
	{
		if (match("*objectlog*", out_files[file_idx]))
		{
			U32 now = time(NULL);
			U32 file_time = fileLastChanged(out_files[file_idx]);
			if (now-file_time > 60*60*48) // Delete logs after 48 hours
			{
				printf("Deleting log: %s\n", out_files[file_idx]);
				fileForceRemove(out_files[file_idx]);
			}
		}
	}
	fileScanDirFreeNames(out_files);

	logSetFileOptions_Filename("objectLog",false,360,0,1);

	objectLibraryFreeModelsAndEditingLib();
	RefSystem_ClearDictionary(g_ObjectLibraryDictionary, true);

	objectLibraryPreLoadFile(NULL); // Load rootmods

#if 0 // Shared memory disabled until I can get it working --TomY
	resLoadResourcesFromDisk(g_ObjectLibraryDictionary, "object_library", ".modelnames;.objlib", "objlib.bin",
							 PARSER_BINS_ARE_SHARED | RESOURCELOAD_SHAREDMEMORY | PARSER_NO_RELOAD);
#else
	resLoadResourcesFromDisk(g_ObjectLibraryDictionary, "object_library", ".modelnames;.objlib", "objlib.bin",
							 PARSER_BINS_ARE_SHARED | PARSER_NO_RELOAD);
#endif

	objectLibraryPostLoadFile(); // Destroy rootmods

	FOR_EACH_IN_REFDICT(g_ObjectLibraryDictionary, GroupDef, def)
	{
		if (objectLibraryGroupEditable(def))
		{
			if (!stashFindPointer(g_ObjectLibrary.stLockedFiles, def->filename, NULL))
			{
				GroupDefLockedFile *file = StructCreate(parse_GroupDefLockedFile);
				file->filename = def->filename;
				stashAddPointer(g_ObjectLibrary.stLockedFiles, def->filename, file, false);
			}
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_REFDICT(g_ObjectLibraryDictionary, GroupDef, def)
	{
		if (def->root_id == 0)
		{
			objectLibraryUpdateChildren(def, def->name_uid);
		}
	}
	FOR_EACH_END;

	objectLibraryRefreshRootsIfExist();

	groupLibMarkBadChildren(&g_ObjectLibrary.sObjectLibraryLib);
	groupLibMarkBadChildren(&g_ObjectLibrary.sObjectEditingLib);

	g_ObjectLibrary.bLoaded = 1;
	g_ObjectLibrary.bInited = 1;
}

//////////////////////////////////////////////////////////////////////////
// Dynamic reload

void reloadObjectLibraryFile(const char *relpath)
{
	bool load_rootmods = false;
	char path_buf[MAX_PATH];
	char rootmods_buf[MAX_PATH] = { 0 };

	filelog_printf("objectLog", "Reloading %s.", relpath);
	loadstart_printf( "Reloading %s...", relpath );

	strcpy(path_buf, relpath);

	if (strEndsWith(path_buf, MODELNAMES_EXTENSION))
	{
		load_rootmods = true;
		changeFileExt(path_buf, ROOTMODS_EXTENSION, rootmods_buf);
	}
	else if (strEndsWith(path_buf, ROOTMODS_EXTENSION))
	{
		load_rootmods = true;
		strcpy(rootmods_buf, path_buf);
		changeFileExt(rootmods_buf, MODELNAMES_EXTENSION, path_buf);
	}

	if (load_rootmods)
	{
		fileWaitForExclusiveAccess(rootmods_buf);
		objectLibraryPreLoadFile(rootmods_buf); // Load rootmods
	}

	fileWaitForExclusiveAccess(path_buf);
	ParserReloadFileToDictionaryWithFlags(path_buf, g_ObjectLibraryDictionary, load_rootmods ? PARSER_IGNORE_EXTENSIONS : 0);

	if (load_rootmods)
		objectLibraryPostLoadFile(); // Destroy rootmods

	loadend_printf( "done" );

	g_ObjectLibrary.iLastUpdated++;
}

void objectLibraryDoneReloading(void)
{
	int i;
	int *eaiEditingDefs = NULL;

	if (!g_ObjectLibrary.bNeedsReload)
		return;

	{
		GroupDef **defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectEditingLib);
		FOR_EACH_IN_EARRAY(defs, GroupDef, editing_def)
		{
			eaiPush(&eaiEditingDefs, editing_def->name_uid);
		}
		FOR_EACH_END;
	}

	for (i = 0; i < eaiSize(&eaiEditingDefs); i++)
	{
		GroupDef *def = objectLibraryGetGroupDef(eaiEditingDefs[i], false);
		if (def)
		{
			//printf("Refreshing def %s (%d)...\n", def->name_str, def->name_uid);
			objectLibraryGetEditingCopy(def, false, true);
		}
	}
	eaiDestroy(&eaiEditingDefs);

	g_ObjectLibrary.bNeedsReload = false;

	objectLibraryConsistencyCheck(true);
	filelog_printf("objectLog", "Editing defs updated.");
}

//////////////////////////////////////////////////////////////////////////
// Per-frame updates

void objectLibraryOncePerFrame(void)
{
	ResourceDictionaryInfo *info = resDictGetInfo("ObjectLibrary");
	if (eaSize(&info->ppInfos) != g_ObjectLibrary.iDictRefs)
	{
		g_ObjectLibrary.iLastUpdated++;
		g_ObjectLibrary.iDictRefs = eaSize(&info->ppInfos);
	}
}

//////////////////////////////////////////////////////////////////////////
// Getters

bool objectLibraryLoaded(void)
{
	return g_ObjectLibrary.bLoaded;
}

bool objectLibraryInited(void)
{
	return g_ObjectLibrary.bInited;
}

int objectLibraryLastUpdated(void)
{
	return g_ObjectLibrary.iLastUpdated;
}

GroupDefLib *objectLibraryGetDefLib(void)
{
	return &g_ObjectLibrary.sObjectLibraryLib;
}

//////////////////////////////////////////////////////////////////////////
// GroupDef retrieval

GroupDef *objectLibraryGetGroupDef(int obj_uid, bool editing_copy)
{
	char id_str[64];
	GroupDef *ret;

	if (obj_uid == -1)
		return g_ObjectLibrary.pDummyGroup;

	sprintf(id_str, "%d", obj_uid);
	ret = RefSystem_ReferentFromString(g_ObjectLibraryDictionary, id_str);

	if (ret)
	{
		// Sanity checks
		assert(ret->name_uid == obj_uid);
		if(objectLibraryInited())
			assert(ret->def_lib == &g_ObjectLibrary.sObjectLibraryLib);

		if (editing_copy)
			ret = objectLibraryGetEditingCopy(ret, true, false);
	}

	return ret;
}

int objectLibraryUIDFromObjName(const char *obj_name)
{
	int id;

	if (!obj_name)
		return 0;

	if (stashFindInt(g_ObjectLibrary.sObjectLibraryLib.def_name_table, allocAddString(obj_name), &id))
		return id;

	return 0;
}

bool objectLibraryGroupNameIsUID(const char *obj_name, int *out_id)
{
	int i;
	if (!obj_name)
		return false;
	for (i = (int)strlen(obj_name)-1; i >= 0; --i)
	{
		if (obj_name[i] != '-' && (obj_name[i] < '0' || obj_name[i] > '9'))
		{
			return false;
		}
	}
	if (sscanf(obj_name, "%d", out_id) == 1)
		return true;
	return false;
}

GroupDef *objectLibraryGetGroupDefByName(const char *obj_name, bool editing_copy)
{
	int id;
	if (objectLibraryGroupNameIsUID(obj_name, &id))
	{
		return objectLibraryGetGroupDef(id, editing_copy);
	}
	else
	{
		id = objectLibraryUIDFromObjName(obj_name);
		if (id != 0)
		{
			GroupDef *ret = objectLibraryGetGroupDef(id, editing_copy);

			if (ret)
			{
				// Sanity checks
				assert(ret->name_str == allocAddString(obj_name));
			}

			return ret;
		}
	}
	return NULL;
}

GroupDef *objectLibraryGetGroupDefFromRef(const GroupDefRef *def_ref, bool editing_copy)
{
	GroupDef *def;

	// lookup by uid first
	def = objectLibraryGetGroupDef(def_ref->name_uid, editing_copy);
	if (!def)
	{
		// lookup by name
		def = objectLibraryGetGroupDefByName(def_ref->name_str, editing_copy);
	}
	return def;
}

ResourceInfo *objectLibraryGetResource(const GroupDefRef *def_ref)
{
	GroupDef *def = objectLibraryGetGroupDefFromRef(def_ref, false);

	if (!def)
		return NULL;

	return resGetInfo(g_ObjectLibraryDictionary, def->name_str);
}

GroupDef *objectLibraryGetGroupDefFromResource(const ResourceInfo *info, bool editing_copy)
{
	int id;
	if (!info || !info->resourceName || sscanf(info->resourceName, "%d", &id) != 1)
		return NULL;
	return objectLibraryGetGroupDef(id, editing_copy);
}

GroupDef *objectLibraryGetDummyGroupDef(void)
{
	if (!g_ObjectLibrary.pDummyGroup)
	{
		g_ObjectLibrary.pDummyGroup = StructCreate(parse_GroupDef);
		g_ObjectLibrary.pDummyGroup->name_uid = -1;
		g_ObjectLibrary.pDummyGroup->name_str = allocAddString("(DELETED)");
		g_ObjectLibrary.pDummyGroup->def_lib = &g_ObjectLibrary.sObjectEditingLib;
	}

	return g_ObjectLibrary.pDummyGroup;
}

//////////////////////////////////////////////////////////////////////////
// Hierarchical group tree

static bool defCheckPackStatusInfo(ResourceInfo *def, void *user_data)
{
	return 1;
}

ResourceGroup *objectLibraryGetRoot()
{
	return g_ObjectLibrary.pResourceGroupRoot;
}

void objectLibraryRefreshRootsIfExist(void)
{
	if (g_ObjectLibrary.pResourceGroupRoot || areEditorsPossible())
	{
		objectLibraryRefreshRoot();
	}
}

int objectLibraryResourceSort(const ResourceInfo **a, const ResourceInfo **b)
{
	if (!(*a) || !(*a)->resourceNotes || !(*b) || !(*b)->resourceNotes)
		return 0;
	return stricmp((*a)->resourceNotes, (*b)->resourceNotes);
}

ResourceGroup *objectLibraryRefreshRoot(void)
{
	PERFINFO_AUTO_START_FUNC();
	if (!g_ObjectLibrary.pResourceGroupRoot)
	{
		g_ObjectLibrary.pResourceGroupRoot = StructCreate(parse_ResourceGroup);
	}

	resBuildGroupTreeEx(g_ObjectLibraryDictionary, g_ObjectLibrary.pResourceGroupRoot, objectLibraryResourceSort);
	g_ObjectLibrary.pResourceGroupRoot->pchName = StructAllocString("object_library");

	PERFINFO_AUTO_STOP();

	return g_ObjectLibrary.pResourceGroupRoot;
}

//////////////////////////////////////////////////////////////////////////
// Editing copies

void objectLibraryAddEditingCopy(GroupDef *def)
{
	stashIntAddPointer(g_ObjectLibrary.stObjectEditingImports, def->name_uid, def, true);
}

GroupDef *objectLibraryGetEditingCopy(GroupDef *def, bool create, bool overwrite)
{
	GroupDef *ret_group, *import_def;

	if (!def)
		return NULL;

	if (!groupIsObjLib(def))
		return def;

	if(def->name_uid == -1)
		return g_ObjectLibrary.pDummyGroup;

	// Check to see if the group already exists in editing_lib
	ret_group = groupLibFindGroupDef(&g_ObjectLibrary.sObjectEditingLib, def->name_uid, false);
	if (ret_group && !overwrite)
	{
		return ret_group;
	}

	if (!create && (!ret_group || !overwrite))
		return NULL;

	if (!ret_group && stashIntFindPointer(g_ObjectLibrary.stObjectEditingImports, def->name_uid, &import_def))
		return NULL; // Deleted

	// Copy this group def to editing_lib
	if (!ret_group || !overwrite)
	{
		ret_group = groupLibCopyGroupDef(&g_ObjectLibrary.sObjectEditingLib, def->filename, def, def->name_str, false, true, true, 0, false);
		stashIntAddPointer(g_ObjectLibrary.stObjectEditingImports, def->name_uid, def, true);
	}
	else
	{
		if (StructCompare(parse_GroupDef, def, ret_group, 0, 0, 0) != 0)
		{
			int i;
			if (gbObjectLibraryDebugEditingCopies)
			{
				char *diff_str = NULL;
				StructWriteTextDiff(&diff_str, parse_GroupDef, def, ret_group, 0, 0, 0, 0);
				printf("DEF %d (%s): %s\n", def->name_uid, def->name_str, diff_str);
				estrDestroy(&diff_str);
			}
			StructCopy(parse_GroupDef, def, ret_group, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, 0, 0);
			for (i = 0; i < eaSize(&ret_group->children); i++)
			{
				GroupDef *child_def = groupChildGetDef(ret_group, ret_group->children[i], true);
				groupChildInitialize(ret_group, i, child_def, ret_group->children[i]->mat, ret_group->children[i]->scale, ret_group->children[i]->seed, ret_group->children[i]->uid_in_parent);
			}
			groupFixupAfterRead(ret_group, false);
			groupPostLoad(ret_group);
			groupDefRefresh(ret_group);
		}
	}

	return ret_group;
}

GroupDef *objectLibraryGetEditingGroupDef(int obj_uid, bool allow_temporary_defs)
{
	return groupLibFindGroupDef(&g_ObjectLibrary.sObjectEditingLib, obj_uid, allow_temporary_defs);
}

GroupDefLib *objectLibraryGetEditingDefLib(void)
{
	return &g_ObjectLibrary.sObjectEditingLib;
}

void objectLibraryFreeModelsAndEditingLib(void)
{
	GroupDef **lib_defs;
	int i;

	if (resIsDictionaryEditMode("ObjectLibrary"))
	{
		lib_defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectLibraryLib);
		for (i = 0; i < eaSize(&lib_defs); i++)
			lib_defs[i]->model = NULL;
	}

	lib_defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectEditingLib);
	for (i = eaSize(&lib_defs)-1; i >= 0; --i)
	{
		GroupDef *def = lib_defs[i];
		stashIntRemovePointer(g_ObjectLibrary.sObjectEditingLib.defs, def->name_uid, NULL);
		stashRemoveInt(g_ObjectLibrary.sObjectEditingLib.def_name_table, def->name_str, NULL);
		groupDefFree(def);
	}

	stashTableClear(g_ObjectLibrary.sObjectEditingLib.defs);
	stashTableClear(g_ObjectLibrary.sObjectEditingLib.def_name_table);
	stashTableClear(g_ObjectLibrary.sObjectEditingLib.temporary_defs);
	stashTableClear(g_ObjectLibrary.stObjectEditingImports);
}

//////////////////////////////////////////////////////////////////////////
// Setting groups/files editable

void objectLibraryGetWritePath(const char *in_path, char *out_path, size_t out_path_size)
{
	if (strEndsWith(in_path, MODELNAMES_EXTENSION))
		changeFileExt_s(in_path, ROOTMODS_EXTENSION, SAFESTR2(out_path));
	else
		strcpy_s(SAFESTR2(out_path), in_path);
}

bool objectLibraryGroupEditable(GroupDef *def)
{
	char id_str[64];
	char file_path[MAX_PATH];

	if (!def->filename)
		return false;

	objectLibraryGetWritePath(def->filename, SAFESTR(file_path));
	if (!fileExists(file_path))
		return false;

	if (def->is_new)
		return true;

	sprintf(id_str, "%d", def->name_uid);
	return resIsWritable(OBJECT_LIBRARY_DICT, id_str);
}

bool objectLibraryGroupSetEditable(GroupDef *def)
{
	ResourceActionList actions = { 0 };
	char file_path[MAX_PATH];

	objectLibraryGetWritePath(def->filename, SAFESTR(file_path));

	assert(!def->def_lib->zmap_layer);
	assert(def->def_lib == &g_ObjectLibrary.sObjectEditingLib || 
		def->def_lib == &g_ObjectLibrary.sObjectLibraryLib);

	if (!fileExists(file_path))
	{
		char out_filename[MAX_PATH];
		FILE *fOut;
		fileLocateWrite(file_path, out_filename);
		makeDirectoriesForFile(out_filename);
		fOut = fopen(out_filename, "w");
		if (fOut)
		{
			fprintf(fOut, " ");
			fclose(fOut);
		}
		if (!fileExists(file_path))
			return false;
	}

	resSetDictionaryEditMode(OBJECT_LIBRARY_DICT, true);
	resSetDictionaryEditMode( gMessageDict, true );
	resAddRequestLockResource( &actions, OBJECT_LIBRARY_DICT, def->name_str, def);
	resRequestResourceActions( &actions );
	StructDeInit( parse_ResourceActionList, &actions );

	return groupIsEditable(def);
}

bool objectLibrarySetFileEditable(const char *filename)
{
	GimmeErrorValue ret;
	GroupDefLockedFile *file;

	if (!fileExists(filename))
	{
		FILE *fOut;
		char path_copy[MAX_PATH];
		strcpy(path_copy, filename);
		mkdirtree(path_copy);
		fOut = fileOpen(filename, "w");
		if (!fOut)
			return false;
		fprintf(fOut, " ");
		fclose(fOut);
	}

	ret = gimmeDLLDoOperation(filename, GIMME_CHECKOUT, GIMME_QUIET);
	if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_ALREADY_DELETED)
	{
		const char *lockee;
		if (ret == GIMME_ERROR_ALREADY_CHECKEDOUT && (lockee = gimmeDLLQueryIsFileLocked(filename))) {
			Alertf("File \"%s\" unable to be checked out, currently checked out by %s", filename, lockee);
		} else {
			Alertf("File \"%s\" unable to be checked out (%s)", filename, gimmeDLLGetErrorString(ret));
		}
		return false;
	}

	if (!stashFindPointer(g_ObjectLibrary.stLockedFiles, filename, &file))
	{
		file = StructCreate(parse_GroupDefLockedFile);
		file->filename = allocAddFilename(filename);
		stashAddPointer(g_ObjectLibrary.stLockedFiles, file->filename, file, false);
		worldIncModTime();
	}

	return true;
}

void objectLibraryGetEditableFiles(GroupDefLockedFile ***file_list)
{
	FOR_EACH_IN_STASHTABLE(g_ObjectLibrary.stLockedFiles, GroupDefLockedFile, file)
		eaPush(file_list, file);
	FOR_EACH_END
}

const char *objectLibraryGetFilePath(GroupDefLockedFile *file)
{
	return file->filename;
}

//////////////////////////////////////////////////////////////////////////
// Group saving

bool objectLibraryGetUnsaved(void)
{
	GroupDef **edit_lib_defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectEditingLib);
	int i;
	for (i = 0; i < eaSize(&edit_lib_defs); i++)
	{
		GroupDef *def = edit_lib_defs[i];
		if (def->save_mod_time > 0)
		{
			return true;
		}
	}
	return false;
}

bool objectLibrarySave(const char *filename)
{
	GroupDef **edit_lib_defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectEditingLib);
	ResourceActionList actions = { 0 };
	bool ret = true;
	GroupDef **modified_defs = NULL;

	resSetDictionaryEditMode(OBJECT_LIBRARY_DICT, true);
	resSetDictionaryEditMode( gMessageDict, true );

	FOR_EACH_IN_EARRAY(edit_lib_defs, GroupDef, def)
	{
		if (def->save_mod_time > 0)
		{
			if ((filename == NULL) || (strcmp(filename, def->filename) == 0))
			{
				char uid_str[64];
				assert(groupIsEditable(def));

				groupFixupBeforeWrite(def, true);

				groupDefFixupMessages(def);

				sprintf(uid_str, "%d", def->name_uid);
				resAddRequestLockResource( &actions, OBJECT_LIBRARY_DICT, uid_str, def);
				resAddRequestSaveResource( &actions, OBJECT_LIBRARY_DICT, uid_str, def);

				eaPush(&modified_defs, def);
			}
		}
	}
	FOR_EACH_END;
	FOR_EACH_IN_STASHTABLE2(g_ObjectLibrary.stObjectEditingImports, elem)
	{
		char uid_str[64];
		int id = stashElementGetIntKey(elem);
		if (groupLibFindGroupDef(&g_ObjectLibrary.sObjectEditingLib, id, false))
			continue;

		// This object has been deleted
		printf("Purging def %d\n", id);
		sprintf(uid_str, "%d", id);
		resAddRequestLockResource( &actions, OBJECT_LIBRARY_DICT, uid_str, NULL);
		resAddRequestSaveResource( &actions, OBJECT_LIBRARY_DICT, uid_str, NULL);

		stashIntRemovePointer(g_ObjectLibrary.stObjectEditingImports, id, NULL);
	}
	FOR_EACH_END;
	resRequestResourceActions( &actions );

	if( actions.eResult == kResResult_Failure ) {
		int it;
		for( it = 0; it != eaSize( &actions.ppActions ); ++it ) {
			if( actions.ppActions[ it ]->eResult != kResResult_Failure ) {
				continue;
			}

			Errorf( "Error saving %s: %s", actions.ppActions[ it ]->pResourceName,
				actions.ppActions[ it ]->estrResultString );
			ret = false;
		}
	}
	else
	{
		FOR_EACH_IN_EARRAY(modified_defs, GroupDef, def)
		{
			def->all_mod_time = 0;
			def->group_mod_time = 0;
			def->save_mod_time = 0;
		}
		FOR_EACH_END;
	}
	eaDestroy(&modified_defs);

	StructDeInit( parse_ResourceActionList, &actions );

	return ret;
}

//////////////////////////////////////////////////////////////////////////
// Resource dictionary events

static void objlibServerRequestSendMessageUpdate(DisplayMessage *pMsg, ResourceCache *pCache)
{
	ResourceDictionary *dict = resGetDictionary( gMessageDict );
	const char *pMsgKey;
	
	assert( !pMsg->pEditorCopy );

	pMsgKey = REF_STRING_FROM_HANDLE( pMsg->hMessage );
	if (pMsgKey)
		resServerRequestSendResourceUpdate( dict, pMsgKey, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE );
}

void objectLibraryHandleEvent(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	GroupDef *def = pResource;
	GroupDefLockedFile *file = NULL;

	if (!def)
		def = objectLibraryGetGroupDefByName(pResourceName, false);
	if (!def)
		return;

	switch (eType)
	{
	xcase RESEVENT_RESOURCE_ADDED:
		groupLibAddGroupDef(&g_ObjectLibrary.sObjectLibraryLib, def, NULL);
		g_ObjectLibrary.iLastUpdated++;
		if (wlIsClient())
		{
			groupFixupAfterRead(def, true); 
		}
		worldIncModTime();
		if (objectLibraryInited())
		{
			if (!isProductionMode())
				filelog_printf("objectLog", "RESEVENT_RESOURCE_ADDED: %s (%d)", def->name_str, def->name_uid);
			g_ObjectLibrary.bNeedsReload = true;
			//eaiPushUnique(&g_ObjectLibrary.eaiReloadedDefs, def->name_uid);
		}

	xcase RESEVENT_RESOURCE_REMOVED:
		groupLibRemoveGroupDef(&g_ObjectLibrary.sObjectLibraryLib, def);
		groupLibRemoveGroupDef(&g_ObjectLibrary.sObjectEditingLib, def);

		g_ObjectLibrary.bNeedsReload = true;
		//eaiFindAndRemove(&g_ObjectLibrary.eaiReloadedDefs, def->name_uid);
		g_ObjectLibrary.iLastUpdated++;
		worldIncModTime();
		if (objectLibraryInited() && !isProductionMode())
			filelog_printf("objectLog", "RESEVENT_RESOURCE_REMOVED: %s (%d)", def->name_str, def->name_uid);

	xcase RESEVENT_RESOURCE_MODIFIED:
		// Remove and re-add the def to the def lib, and perform fixup
		groupLibRemoveGroupDef(&g_ObjectLibrary.sObjectLibraryLib, def);
		if (groupLibAddGroupDef(&g_ObjectLibrary.sObjectLibraryLib, def, NULL))
		{
			g_ObjectLibrary.bNeedsReload = true;
			//eaiPushUnique(&g_ObjectLibrary.eaiReloadedDefs, def->name_uid);
		}
		g_ObjectLibrary.iLastUpdated++;
		if (wlIsClient())
		{
			groupFixupAfterRead(def, true); 
		}
		groupDefRefresh(def);
		if (objectLibraryInited() && !isProductionMode())
			filelog_printf("objectLog", "RESEVENT_RESOURCE_MODIFIED: %s (%d)", def->name_str, def->name_uid);
		

	xcase RESEVENT_RESOURCE_LOCKED:
		// Make sure the client has all the messages in this objlib piece
		if (wlIsServer()) {
			int id = resGetLockOwner(pDictName, pResourceName);
			ResourceCache* pCache = resGetCacheFromUserID(id);
			
			if( !id ) {
				Errorf( "A RESOURCE_LOCKED message was sent, but the file is not yet locked.  Strange.");
			} else if( !pCache ) {
				Errorf( "A RESOURCE_LOCKED message was sent, but the user who locked the file is not connected.  Strange." );
			} else {
				langForEachDisplayMessage(parse_GroupProperties, &def->property_structs, objlibServerRequestSendMessageUpdate, pCache);
			}

			// Messages will get pushed later, when the reply is being sent
		}
		
		// Add the filename to the locked filenames table
		if (fileExists(def->filename))
		{
			if (!stashFindPointer(g_ObjectLibrary.stLockedFiles, def->filename, NULL))
			{
				file = StructCreate(parse_GroupDefLockedFile);
				file->filename = def->filename;
				stashAddPointer(g_ObjectLibrary.stLockedFiles, def->filename, file, false);
				worldIncModTime();
			}
		}

	xcase RESEVENT_RESOURCE_UNLOCKED:
		// Remove the filename from the locked filenames table
		if (stashFindPointer(g_ObjectLibrary.stLockedFiles, def->filename, &file))
		{
			stashRemovePointer(g_ObjectLibrary.stLockedFiles, def->filename, &file);
			StructDestroy(parse_GroupDefLockedFile, file);
			worldIncModTime();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// for GetVrml:

static GlobalUIDs guf[2] = {0};

static void gufClear(GlobalUIDs *guf_ptr)
{
	stashTableDestroy(guf_ptr->entry_hash);
	guf_ptr->entry_hash = NULL;
	StructDeInit(parse_GlobalUIDs, guf_ptr);
}

static void gufLoad(const char *guf_fname, GlobalUIDs *guf_ptr, bool is_core)
{
	int i;

	if (guf_ptr->entry_hash)
		gufClear(guf_ptr);

	ParserLoadFiles(NULL, guf_fname, NULL, PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_GlobalUIDs, guf_ptr);
	if (!is_core)
	{
		for (i = eaSize(&guf_ptr->entries)-1; i >= 0; --i)
		{
			GUIDEntry *entry = guf_ptr->entries[i];
			if (groupIsCoreUID(entry->uid))
			{
				printf("Found core UID (%d) for object \"%s\" in project GUF file, removing.\n", entry->uid, entry->modelname);
				eaRemove(&guf_ptr->entries, i);
				StructDestroy(parse_GUIDEntry, entry);
			}
		}
	}

	guf_ptr->entry_hash = stashTableCreateWithStringKeys(4096, StashDefault); // shallow copy string keys

	for (i = 0; i < eaSize(&guf_ptr->entries); ++i)
	{
		assert(stashAddInt(guf_ptr->entry_hash, guf_ptr->entries[i]->modelname, guf_ptr->entries[i]->uid, false));
	}
}

static void gufRead(bool is_core)
{
	char guf_fname[MAX_PATH];

	if (is_core)
		fileLocateWrite("object_library/core_global_uids.guf", guf_fname);
	else
		fileLocateWrite("object_library/global_uids.guf", guf_fname);

#if !PLATFORM_CONSOLE
	filelog_printf("C:/guf_update.log", "[%d] Reading %s (%u).", GetCurrentProcessId(), guf_fname, fileSize(guf_fname));
#endif
	gimmeDLLForceDirtyBit(guf_fname);
	if (!guf[!!is_core].entries || !gimmeDLLQueryIsFileLatest(guf_fname))
	{
		int ret;
		if (!gimmeDLLQueryIsFileLatest(guf_fname))
		{
#if !PLATFORM_CONSOLE
			filelog_printf("C:/guf_update.log", "[%d] Updating %s (%u).", GetCurrentProcessId(), guf_fname, fileSize(guf_fname));
#endif
			ret = gimmeDLLDoOperation(guf_fname, GIMME_GLV, GIMME_QUIET);
			if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_NO_DLL)
				FatalErrorFilenamef(guf_fname, "Unable to get latest on the global uid file!");
		}
#if !PLATFORM_CONSOLE
		filelog_printf("C:/guf_update.log", "[%d] Done updating %s (%u).", GetCurrentProcessId(), guf_fname, fileSize(guf_fname));
#endif
		gufLoad(guf_fname, &guf[!!is_core], is_core);
	}
}

static void gufMerge(const char *guf_fname, bool is_core)
{
	GlobalUIDs new_guf={0};
	int i, uid;

	gufLoad(guf_fname, &new_guf, is_core);
	for (i = 0; i < eaSize(&new_guf.entries); ++i)
	{
		if (!stashFindInt(guf[!!is_core].entry_hash, new_guf.entries[i]->modelname, &uid))
		{
			GUIDEntry *entry = StructCreate(parse_GUIDEntry);
			entry->modelname = StructAllocString(new_guf.entries[i]->modelname);
			entry->uid = new_guf.entries[i]->uid;
			eaPush(&guf[!!is_core].entries, entry);
			assert(stashAddInt(guf[!!is_core].entry_hash, entry->modelname, entry->uid, false));
		}
	}

	gufClear(&new_guf);
}

static void gufWrite(bool is_core)
{
	char guf_fname[MAX_PATH];
	int ret, failure_count = 0;

	if (!guf[!!is_core].entries)
		return;

	if (is_core)
	{
		assert(fileCoreDataDir()); // Shouldn't get here if we have no core folder, otherwise things might be going horribly wrong
		fileLocateWrite("object_library/core_global_uids.guf", guf_fname);
	} else
		fileLocateWrite("object_library/global_uids.guf", guf_fname);

#if !PLATFORM_CONSOLE
	filelog_printf("C:/guf_update.log", "[%d] Checking out %s (%u).", GetCurrentProcessId(), guf_fname, fileSize(guf_fname));
#endif
	gimmeDLLForceDirtyBit(guf_fname);
	ret = gimmeDLLDoOperation(guf_fname, GIMME_CHECKOUT, GIMME_QUIET);
	while (ret == GIMME_ERROR_NOTLOCKEDBYYOU || ret == GIMME_ERROR_ALREADY_CHECKEDOUT)
	{
		++failure_count;
		if (failure_count >= 3)
		{
			char msg[1024];
			sprintf(msg, "Unable to check out \"%s\".  Please ask %s to check it in, then press ok.", guf_fname, gimmeDLLQueryIsFileLocked(guf_fname));
			errorDialog(NULL, msg, "Error", NULL, 1);
		}
		if (failure_count >= 5)
			break;
		Sleep(500);
#if !PLATFORM_CONSOLE
		filelog_printf("C:/guf_update.log", "[%d] %s was locked, trying again (%u).", GetCurrentProcessId(), guf_fname, fileSize(guf_fname));
#endif
		gimmeDLLForceDirtyBit(guf_fname);
		ret = gimmeDLLDoOperation(guf_fname, GIMME_CHECKOUT, GIMME_QUIET);
	}

	if (ret != GIMME_ERROR_NO_DLL && isGimmeErrorFatal(ret))
		FatalErrorf("Unable to check out \"%s\".  Please ask %s to check it in, then try again.", guf_fname, gimmeDLLQueryIsFileLocked(guf_fname));

#if !PLATFORM_CONSOLE
	filelog_printf("C:/guf_update.log", "[%d] Done checking out %s (%u).", GetCurrentProcessId(), guf_fname, fileSize(guf_fname));
#endif

	gufMerge(guf_fname, is_core);

	ParserWriteTextFile(guf_fname, parse_GlobalUIDs, &guf[!!is_core], 0, 0);

#if !PLATFORM_CONSOLE
	filelog_printf("C:/guf_update.log", "[%d] Checking in %s (%u).", GetCurrentProcessId(), guf_fname, fileSize(guf_fname));
#endif
	ret = gimmeDLLDoOperation(guf_fname, GIMME_CHECKIN, GIMME_QUIET);
	if (ret != GIMME_ERROR_NO_DLL && isGimmeErrorFatal(ret))
		FatalErrorf("Unable to check in \"%s\".", guf_fname);
#if !PLATFORM_CONSOLE
	filelog_printf("C:/guf_update.log", "[%d] Done checking in %s (%u).", GetCurrentProcessId(), guf_fname, fileSize(guf_fname));
#endif
}

static int gufGetUID(bool is_core, const char *modelname, bool do_read)
{
	int uid;
	
	if (do_read)
	{
		gufRead(true);
		if (!is_core)
			gufRead(false);
	}

	// check core guf first
	if (stashFindInt(guf[1].entry_hash, modelname, &uid))
		return uid;

	if (!is_core)
	{
		// check project guf
		if (stashFindInt(guf[0].entry_hash, modelname, &uid))
			return uid;
	}

	return 0;
}

static int gufSetUID(bool is_core, const char *modelname, int uid)
{
	GUIDEntry *entry;
	if (gufGetUID(is_core, modelname, false) != 0)
		return 0;
	entry = StructCreate(parse_GUIDEntry);
	entry->modelname = StructAllocString(modelname);
	entry->uid = uid;
	eaPush(&guf[!!is_core].entries, entry);
	assert(stashAddInt(guf[!!is_core].entry_hash, entry->modelname, entry->uid, false));

	return 1;
}

int objectLibraryWriteModelnamesToFile(const char *fname, SimpleGroupDef **groups, bool no_checkout, bool is_core)
{
	char fname_out[MAX_PATH];
	int i, ret, guf_changed = 0;
	char	*filedata=0;

	fileLocateWrite(fname, fname_out);
	mkdirtree(fname_out);

	for (i = 0; i < eaSize(&groups); ++i)
	{
		SimpleGroupDef *group = groups[i];
		if (group->name_uid != 0)
			continue;
		if (!(group->name_uid = gufGetUID(is_core, group->name_str, true)))
			group->name_uid = worldGenerateDefNameUID(group->name_str, NULL, true, is_core);
	}

	if (no_checkout)
	{
		if (_chmod(fname_out, _S_IREAD | _S_IWRITE) != 0)
			verbose_printf("Unable to change permissions for %s\n", fname_out);
	}
	else
	{
		ret = gimmeDLLDoOperation(fname_out, GIMME_CHECKOUT, GIMME_QUIET);
		if (ret != GIMME_ERROR_NO_DLL && ret != GIMME_ERROR_ALREADY_DELETED && !gimmeDLLQueryIsFileLockedByMeOrNew(fname_out))
		{
			printfColor(COLOR_RED|COLOR_BRIGHT, "Unable to check out file!");
			ErrorFilenamef(fname_out, "Unable to checkout file!");
			return 0;
		}
	}
	
	if (!worldSaveSimpleDefs(fname_out, groups))
	{
		ErrorFilenamef(fname_out, "Unable to open file for writing!");
		return 0;
	}

	gufRead(true);
	if (!is_core)
		gufRead(false);
	
	for (i = 0; i < eaSize(&groups); ++i)
		guf_changed = gufSetUID(is_core, groups[i]->name_str, groups[i]->name_uid) || guf_changed;
	

	if (guf_changed)
		gufWrite(is_core);
	
	return 1;
}

//////////////////////////////////////////////////////////////////////////////////////
// Testing Only
//////////////////////////////////////////////////////////////////////////////////////

int sortDefsBeforeDump(const GroupDef **pDef1, const GroupDef **pDef2)
{
	if ((*pDef1)->def_lib->zmap_layer != NULL && (*pDef2)->def_lib->zmap_layer)
	{
		int diff = strcmp((*pDef1)->def_lib->zmap_layer->filename, (*pDef2)->def_lib->zmap_layer->filename);
		if (diff != 0)
			return diff;
	}
	return (*pDef1)->name_uid - (*pDef2)->name_uid;
}

int sortProperties(const char **prop1, const char **prop2)
{
	return strcmp(*prop1, *prop2);
}

void objectLibraryDumpGroupDef(FILE *fOut, GroupDef *def)
{
	int j, k;
	char **properties = NULL;

	groupPostLoad(def);

	fprintf(fOut, "DEF %d %s\n", def->name_uid, def->name_str);

	for (j = 0; j < eaSize(&def->children); j++)
	{
		GroupChild *child = def->children[j];
		GroupDef *child_def = groupChildGetDef(def, child, false);
		fprintf(fOut, "CHILD %d %s %f %d [%f %f %f][%f %f %f][%f %f %f][%f %f %f]\n", child_def ? child_def->name_uid : 0, child_def ? child_def->name_str : "(null)", child->scale,
			child->seed, child->mat[0][0], child->mat[0][1], child->mat[0][2], child->mat[1][0], child->mat[1][1], child->mat[1][2],
			child->mat[2][0], child->mat[2][1], child->mat[2][2], child->mat[3][0], child->mat[3][1], child->mat[3][2]);
	}

	fprintf(fOut, "BOUNDS (%f %f %f)-(%f %f %f)-(%f %f %f) %f\n",
		def->bounds.min[0], def->bounds.min[1], def->bounds.min[2],
		def->bounds.mid[0], def->bounds.mid[1], def->bounds.mid[2],
		def->bounds.max[0], def->bounds.max[1], def->bounds.max[2],
		def->bounds.radius);
	fprintf(fOut, "FLAGS %X %X %s\n", def->group_flags,def->property_structs.physical_properties.iOccluderFaces, def->is_dynamic ? "Y" : "N");

	if (def->model)
	{
		fprintf(fOut, "MODEL %s (%f %f %f)\n", def->model->name, def->model_scale[0], def->model_scale[1], def->model_scale[2]);
	}

	if (def->group_flags & GRP_HAS_TINT)
	{
		fprintf(fOut, "TINT %f %f %f\n", def->tint_color0[0], def->tint_color0[1], def->tint_color0[2]);
	}

	if (def->group_flags & GRP_HAS_TINT_OFFSET)
	{
		fprintf(fOut, "TINT OFFSET %f %f %f\n", def->tint_color0_offset[0], def->tint_color0_offset[1], def->tint_color0_offset[2]);
	}

	if (def->group_flags & GRP_HAS_MATERIAL_REPLACE)
	{
		fprintf(fOut, "MATERIAL REPLACE %s\n", def->replace_material_name);
	}

	for (j = 0; j < eaSize(&def->texture_swaps); j++)
		fprintf(fOut, "TEXTURE SWAP %s %s\n", def->texture_swaps[j]->orig_name, def->texture_swaps[j]->replace_name);

	for (j = 0; j < eaSize(&def->material_swaps); j++)
		fprintf(fOut, "MATERIAL SWAP %s %s\n", def->material_swaps[j]->orig_name, def->material_swaps[j]->replace_name);

	for (j = 0; j < eaSize(&def->material_properties); j++)
		fprintf(fOut, "MATERIAL PROPERTY %s %f %f %f %f\n", def->material_properties[j]->name, def->material_properties[j]->value[0],
		def->material_properties[j]->value[1], def->material_properties[j]->value[2], def->material_properties[j]->value[3]);

	eaQSort(properties, sortProperties);
	for (j = 0; j < eaSize(&properties); j++)
		 fprintf(fOut, "PROPERTY %s\n", properties[j]);
	eaDestroyEx(&properties, NULL);

	fprintf(fOut, "SCOPE START INDEX %d\n", def->starting_index);
	FOR_EACH_IN_STASHTABLE2(def->name_to_path, el)
	{
		fprintf(fOut, "SCOPE NAME %s %s\n", stashElementGetStringKey(el), (char*)stashElementGetPointer(el));
	}
	FOR_EACH_END

		FOR_EACH_IN_STASHTABLE2(def->name_to_instance_data, el)
	{
		InstanceData *data = (InstanceData*)stashElementGetPointer(el);
		fprintf(fOut, "INSTANCE DATA %s", stashElementGetStringKey(el));
		if (data->room_data)
		{
			for (j = 0; j < eaSize(&data->room_data->actions); j++)
			{
				RoomInstanceMapSnapAction *action = data->room_data->actions[j];
				fprintf(fOut, " [ %d (%f %f) (%f %f) %f %f ]", action->action_type, action->min_sel[0], action->min_sel[1], action->max_sel[0], action->max_sel[1], action->near_plane, action->far_plane);
			}
			fprintf(fOut, " %s", data->room_data->no_photo ? "N" : "Y");
		}
		fprintf(fOut, "\n");
	}
	FOR_EACH_END

	for (j = 0; j < eaSize(&def->logical_groups); j++)
	{
		fprintf(fOut, "LOGICAL GROUP %s -", def->logical_groups[j]->group_name);
		for (k = 0; k < eaSize(&def->logical_groups[j]->child_names); k++)
			fprintf(fOut, " %s", def->logical_groups[j]->child_names[k]);
		fprintf(fOut, "\n");
		// TomY TODO Properties
	}

	{
		char *str = NULL;
		ParserWriteText(&str, parse_GroupProperties, &def->property_structs, 0, 0, 0);
		fprintf(fOut, "%s\n", str);
		estrDestroy(&str);
	}
}

AUTO_COMMAND;
void objectLibraryDumpToFile(void)
{
	GroupDef **lib_defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectLibraryLib);
	FILE *fOut = fopen("C:\\bindiffs_post\\object_library.txt", "w");
	int i;

	if (!fOut)
	{
		printf("FAILED TO OPEN OUTPUT FILE.\n");
		return;
	}

	eaQSort(lib_defs, sortDefsBeforeDump);

	for (i = 0; i < eaSize(&lib_defs); i++)
		objectLibraryDumpGroupDef(fOut, lib_defs[i]);

	fclose(fOut);

	printf("DUMPED %d GROUPDEFS.\n", eaSize(&lib_defs));
}

static char * s_aszCollTypeNames[] =
{
	"None",
	"Full",
	"Permeable (visible)",
	"Permeable (invisible)",
	"Non-blocking camera",
	"Special"
};

bool objectLibraryDumpMapTraverseCB(FILE *fOut, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	int i;
	fprintf(fOut, "**** BEGIN ENTRY %d ****\n", info->traverse_id);

	if (def)
		objectLibraryDumpGroupDef(fOut, def);
	else
		fprintf(fOut, "DEF NULL\n");

	fprintf(fOut, ":MAT [%f %f %f][%f %f %f][%f %f %f][%f %f %f]\n", info->world_matrix[0][0], info->world_matrix[0][1], info->world_matrix[0][2],
		info->world_matrix[1][0], info->world_matrix[1][1], info->world_matrix[1][2],
		info->world_matrix[2][0], info->world_matrix[2][1], info->world_matrix[2][2],
		info->world_matrix[3][0], info->world_matrix[3][1], info->world_matrix[3][2]);
	fprintf(fOut, ":BOUNDS (%f %f %f) - (%f %f %f) - (%f %f %f) %f\n", info->world_min[0], info->world_min[1], info->world_min[2],
		info->world_mid[0], info->world_mid[1], info->world_mid[2],
		info->world_max[0], info->world_max[1], info->world_max[2], info->radius);
	fprintf(fOut, ":SCALE %f %f\n", info->uniform_scale, info->current_scale);
	fprintf(fOut, ":FADE (%f %f %f) %f %s\n", info->fade_mid[0], info->fade_mid[1], info->fade_mid[2], info->fade_radius, info->has_fade_node ? "Y":"N");
	fprintf(fOut, ":LODSCALE %f\n", info->lod_scale);
	fprintf(fOut, ":COLOR %f %f %f %f\n", info->color[0], info->color[1], info->color[2], info->color[3]);
	fprintf(fOut, ":TINTOFFSET %f %f %f\n", info->tint_offset[0], info->tint_offset[1], info->tint_offset[2]);
	fprintf(fOut, ":COLL %d",info->collisionFilterBits);
	fprintf(fOut, ":FLAGS EO%d VI%d UL%d DG%d LD%d HD%d HFD%d MS%d DS%d ID%d FW%d NSC%d NSR%d NO%d PNO%d AT%d DO%d\n",
		info->editor_only, info->visible, info->unlit, info->dummy_group, info->low_detail,
		info->high_detail, info->high_fill_detail, info->map_snap_hidden, info->double_sided_occluder, info->is_debris, info->force_trunk_wind,
		info->no_shadow_cast, info->no_shadow_receive, info->no_occlusion, info->parent_no_occlusion, info->apply_tint_offset,
		info->in_dynamic_object);
	fprintf(fOut, ":SEED %d\n", info->seed);
	fprintf(fOut, ":SPLINE %s\n", info->spline ? "Y":"N"); // TomY TODO Details
	fprintf(fOut, ":SPLINE_INHERIT %d\n", eafSize(&info->inherited_spline.spline_points)); // TomY TODO Details
	fprintf(fOut, ":CURVE_MAT [%f %f %f][%f %f %f][%f %f %f][%f %f %f]\n", info->curve_matrix[0][0], info->curve_matrix[0][1], info->curve_matrix[0][2],
		info->curve_matrix[1][0], info->curve_matrix[1][1], info->curve_matrix[1][2],
		info->curve_matrix[2][0], info->curve_matrix[2][1], info->curve_matrix[2][2],
		info->curve_matrix[3][0], info->curve_matrix[3][1], info->curve_matrix[3][2]);
	fprintf(fOut, ":NODE HEIGHT %f\n", info->node_height);

	if (info->debris_excluders)
	{
		fprintf(fOut, ":DEBRIS_EXCLUDERS ");
		for (i = 0; i < eafSize(&info->debris_excluders); i++)
			fprintf(fOut, " %f", info->debris_excluders[i]);
		fprintf(fOut, "\n");
	}

	if (info->instance_info)
	{
		fprintf(fOut, ":INSTANCE_INFO\n"); // TomY TODO Details
	}

	if (info->material_replace)
		fprintf(fOut, ":MATERIAL REPLACE %s\n", info->material_replace);

	fprintf(fOut, ":CHILD_IDX %d\n", info->parent_entry_child_idx);
	fprintf(fOut, ":VISIBLE_CHILD %d\n", info->visible_child);
	fprintf(fOut, ":LAYERID %d TAG %d\n", info->layer_id_bits, info->tag_id);
	fprintf(fOut, ":REGION %s\n", info->region ? info->region->name : "(null)");
	fprintf(fOut, ":LAYER %s\n", info->layer ? info->layer->filename : "(null)");

	if (info->fx_entry)
	{
		fprintf(fOut, ":FX_ENTRY\n"); // TomY TODO Details
	}
	if (info->parent_entry)
	{
		fprintf(fOut, ":PARENT_ENTRY\n"); // TomY TODO Details
	}
	if (info->animation_entry)
	{
		fprintf(fOut, ":ANIMATION_ENTRY\n"); // TomY TODO Details
	}
	if (info->volume_entry)
	{
		fprintf(fOut, ":VOLUME_ENTRY\n"); // TomY TODO Details
	}

	if (info->volume_def)
	{
		fprintf(fOut, ":VOLUME ");
		objectLibraryDumpGroupDef(fOut, info->volume_def);
	}

	if (info->lod_override)
	{
		fprintf(fOut, ":LODOVERRIDE ");
		for (i = 0; i < eaSize(&info->lod_override->lod_distances); i++)
			fprintf(fOut, " (%f %f %s)", info->lod_override->lod_distances[i]->lod_near, info->lod_override->lod_distances[i]->lod_far, info->lod_override->lod_distances[i]->no_fade ? "N" : "Y");
		fprintf(fOut, "\n");
	}
	if (info->ignore_lod_override)
		fprintf(fOut, ":INGORELODOVERRIDE\n");

	if (info->room)
	{
		fprintf(fOut, ":ROOM %s\n", info->room->def_name); // TomY TODO Details
	}
	if (info->room_partition)
	{
		fprintf(fOut, ":ROOMPARTITION\n"); // TomY TODO Details
	}
	if (info->room_portal)
	{
		fprintf(fOut, ":ROOMPORTAL\n"); // TomY TODO Details
	}
	if (info->exclude_from_room)
		fprintf(fOut, ":EXCLUDEFROMROOM\n");

	if (info->closest_scope)
	{
		fprintf(fOut, ":CLOSEST_SCOPE %s\n", info->closest_scope->def ? info->closest_scope->def->name_str : "(null)"); // TomY TODO Details
	}

	if (info->zmap)
		fprintf(fOut, ":ZMAP %s\n", info->zmap->map_info.filename);

	fprintf(fOut, "=PARENTS");
	for (i = 0; i < eaSize(&inherited_info->parent_defs); i++)
		fprintf(fOut, " (%d %d)", inherited_info->parent_defs[i]->name_uid, inherited_info->idxs_in_parent[i]);
	fprintf(fOut, "\n");
	fprintf(fOut, "=MATERIAL_SWAPS ");
	for (i = 0; i < eaSize(&inherited_info->material_swap_list); i++)
		fprintf(fOut, " %s", inherited_info->material_swap_list[i]);
	fprintf(fOut, "\n");
	fprintf(fOut, "=TEXTURE_SWAPS ");
	for (i = 0; i < eaSize(&inherited_info->texture_swap_list); i++)
		fprintf(fOut, " %s", inherited_info->texture_swap_list[i]);
	fprintf(fOut, "\n");
	for (i = 0; i < eaSize(&inherited_info->material_property_list); i++)
		fprintf(fOut, "=MATERIAL_PROP %s %f %f %f %f\n", inherited_info->material_property_list[i]->name, inherited_info->material_property_list[i]->value[0],
			inherited_info->material_property_list[i]->value[1], inherited_info->material_property_list[i]->value[2], inherited_info->material_property_list[i]->value[3]);

	fprintf(fOut, "**** END ENTRY %d ****\n\n", info->traverse_id);

	return true;
}

AUTO_COMMAND;
void objectLibraryDumpMap(int load)
{
	int i, j, count = 0;
	char filename[256];
	FILE *fOut;

	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		assert(layer);

		if (load > 0)
			layerSetMode(layer, LAYER_MODE_GROUPTREE, false, false, false);

		if (layer->layer_mode == LAYER_MODE_GROUPTREE)
		{
			GroupDefLib *def_lib;
			GroupDef **lib_defs = NULL;

			// Traversal

			sprintf(filename, "C:\\bindiffs_post\\map_%s_%s.txt", zmapInfoGetPublicName(NULL), layer->name);
			fOut = fopen(filename, "w");

			if (!fOut)
			{
				printf("FAILED TO OPEN OUTPUT FILE.\n");
				continue;
			}

			layerGroupTreeTraverse(layer, objectLibraryDumpMapTraverseCB, fOut, false, false);

			fclose(fOut);

			// Defs

			sprintf(filename, "C:\\bindiffs_post\\map_%s_%s_DEFS.txt", zmapInfoGetPublicName(NULL), layer->name);
			fOut = fopen(filename, "w");

			if (!fOut)
			{
				printf("FAILED TO OPEN OUTPUT FILE.\n");
				continue;
			}

			def_lib = layerGetGroupDefLib(layer);
			if (def_lib)
				lib_defs = groupLibGetDefEArray(def_lib);

			eaQSort(lib_defs, sortDefsBeforeDump);

			for (j = 0; j < eaSize(&lib_defs); j++)
				objectLibraryDumpGroupDef(fOut, lib_defs[j]);

			count += eaSize(&lib_defs);

			fclose(fOut);
		}
	}

	printf("DUMPED TRAVERSAL (%d DEFS).\n", count);
}

static int debug_array_size = 0;
static GroupDef *debug_array = NULL;

void objectLibraryDebugCreateBackup()
{
	GroupDef **lib_defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectLibraryLib);
	FILE *fOut = fopen("C:\\objlib_index.txt", "w");
	int i;

	debug_array = calloc(1, sizeof(GroupDef)*eaSize(&lib_defs));

	for (i = 0; i < eaSize(&lib_defs); i++)
	{
		memcpy(&debug_array[i], lib_defs[i], sizeof(GroupDef));

		if (fOut)
			fprintf(fOut, "%d: %d %s\n", i, debug_array[i].name_uid, debug_array[i].name_str);
	}
	debug_array_size = eaSize(&lib_defs);
	if (fOut)
		fclose(fOut);
}

void objectLibraryDebugCheck()
{
	GroupDef **lib_defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectLibraryLib);
	int i;

	if (!debug_array)
		return;

	assert(debug_array_size == eaSize(&lib_defs));
	for (i = 0; i < debug_array_size; i++)
	{
		assert(debug_array[i].name_uid == lib_defs[i]->name_uid);
		assert(debug_array[i].name_str == lib_defs[i]->name_str);
		assert(debug_array[i].filename == lib_defs[i]->filename);
	}
}

bool objectLibraryConsistencyCheck(bool do_assert)
{
	groupLibMarkBadChildren(&g_ObjectLibrary.sObjectLibraryLib);
	groupLibMarkBadChildren(&g_ObjectLibrary.sObjectEditingLib);

	if (!groupLibConsistencyCheckAll(&g_ObjectLibrary.sObjectLibraryLib, do_assert))
		return false;
	if (!groupLibConsistencyCheckAll(&g_ObjectLibrary.sObjectEditingLib, do_assert))
		return false;
	return true;
}

AUTO_COMMAND;
void objectLibraryTouchGroup(int group_id)
{
	GroupDef **lib_defs = groupLibGetDefEArray(&g_ObjectLibrary.sObjectEditingLib);
	FOR_EACH_IN_EARRAY(lib_defs, GroupDef, def)
	{
		if (def->name_uid == group_id || def->root_id == group_id)
		{
			groupDefModify(def, UPDATE_GROUP_PROPERTIES, true);
		}
	}
	FOR_EACH_END;
}

