#include <string.h>
#include <stdio.h>
#include <float.h>
#include "net/netpacketutil.h"

#include "timing.h"
#include "logging.h"
#include "rgb_hsv.h"
#include "fileutil2.h"
#include "qsortG.h"
#include "StringCache.h"

#include "WorldGridPrivate.h"
#include "WorldGridLoadPrivate.h"
#include "ObjectLibrary.h"
#include "wlState.h"

#include "WorldGridLoadPrivate_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

LibFileLoad *loadLibFromDisk(const char *filename);

// quantize positions to 1/10000 of a foot
#define POS_QUANT 10000.f

static void extractMat(GroupChild *child, const Mat4 mat_in)
{
	int		unit,zero;
	Vec3	scale;
	Mat4	mat;

	copyMat4(mat_in, mat);
	unitZeroMat(mat,&unit,&zero);
	if (!unit)
	{
		extractScale(mat,scale); // also normalizes the matrix
		getMat3YPR(mat,child->rot);
	}
	if (!zero)
	{
		copyVec3(mat[3], child->pos);

		child->pos[0] = round(child->pos[0] * POS_QUANT) / POS_QUANT;
		child->pos[1] = round(child->pos[1] * POS_QUANT) / POS_QUANT;
		child->pos[2] = round(child->pos[2] * POS_QUANT) / POS_QUANT;
	}
}

static int cmpScopeEntries(const ScopeTableLoad **scope_a_p, const ScopeTableLoad **scope_b_p)
{
	return stricmp((*scope_a_p)->name, (*scope_b_p)->name);
}

static int cmpInstanceData(const InstanceDataLoad **inst_a_p, const InstanceDataLoad **inst_b_p)
{
	return stricmp((*inst_a_p)->name, (*inst_b_p)->name);
}

// Prepare GroupDef for saving; move data from run-time members to TextParser-friendly ones.
void groupFixupBeforeWriteEx(GroupDef *def, bool clear_invalid, bool silent)
{
	int i;

	// Fixup model name
	if (def->model)
	{
		StructFreeString(def->model_name);
		def->model_name = StructAllocString(def->model->name);
	}

	// Fixup tint & tint offset
	if (def->group_flags & GRP_HAS_TINT)
	{
		def->hasTint0 = true;
		rgbToHsv(def->tint_color0, def->tintColorHSV0);
	}
	else
	{
		def->hasTint0 = false;
		setVec3(def->tintColorHSV0, 0, 0, 0);
	}
	if (def->group_flags & GRP_HAS_TINT_OFFSET)
	{
		def->hasTintOffset0 = true;
		copyVec3(def->tint_color0_offset, def->tintColorOffsetHSV0);
	}
	else
	{
		def->hasTintOffset0 = false;
		setVec3(def->tintColorOffsetHSV0, 0, 0, 0);
	}

	eaDestroyStruct(&def->tex_swap_load, parse_TexSwapLoad);
	// Fixup texture & material swaps
	for (i = 0; i < eaSize(&def->texture_swaps); i++)
	{
		TexSwapLoad *texswap = StructCreate(parse_TexSwapLoad);
		texswap->orig_swap_name = def->texture_swaps[i]->orig_name;
		texswap->rep_swap_name = def->texture_swaps[i]->replace_name;
		eaPush(&def->tex_swap_load, texswap);
	}
	for (i = 0; i < eaSize(&def->material_swaps); i++)
	{
		TexSwapLoad *texswap = StructCreate(parse_TexSwapLoad);
		texswap->orig_swap_name = def->material_swaps[i]->orig_name;
		texswap->rep_swap_name = def->material_swaps[i]->replace_name;
		texswap->is_material = 1;
		eaPush(&def->tex_swap_load, texswap);
	}

	// Fixup children
	for (i = 0; i < eaSize(&def->children); i++)
	{
		GroupChild *child = def->children[i];
		GroupDef *child_def = groupChildGetDef(def, def->children[i], silent);

		// Fixup name & UID
		if (child_def && child_def->filename)
		{
			child->name_uid = child_def->name_uid;
			child->debug_name = child_def->name_str;
		}

		// Fixup mat
		copyVec3(zerovec3, child->pos);
		copyVec3(zerovec3, child->rot);
		extractMat(child, child->mat);
	}

	eaDestroyStruct(&def->scope_entries_load, parse_ScopeTableLoad);
	eaDestroyStruct(&def->instance_data_load, parse_InstanceDataLoad);
	{
		StashTableIterator iter;
		StashElement el;

		// create parsed entries
		stashGetIterator(def->name_to_instance_data, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			const char *name = stashElementGetStringKey(el);
			InstanceData *data = stashElementGetPointer(el);
			InstanceDataLoad *new_load = StructCreate(parse_InstanceDataLoad);
			new_load->name = StructAllocString(name);
			StructCopy(parse_InstanceData, data, &new_load->data, 0, 0, 0);
			eaPush(&def->instance_data_load, new_load);
		}
	}

	// scope data
	if (def->path_to_name)
	{
		StashTableIterator iter;
		StashElement el;

		// clear unused entries
		if(clear_invalid)
			groupDefScopeClearInvalidEntries(def, false);

		// create parsed entries
		stashGetIterator(def->path_to_name, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			ScopeTableLoad *scope_entry;
			const char *path = stashElementGetStringKey(el);
			const char *name = stashElementGetPointer(el);
			
			if (strncmp(GROUP_UNNAMED_PREFIX, name, strlen(GROUP_UNNAMED_PREFIX)) != 0 &&
				!strstri(GROUP_UNNAMED_PREFIX, path))
			{
				scope_entry = StructCreate(parse_ScopeTableLoad);
				eaPush(&def->scope_entries_load, scope_entry);
				scope_entry->path = StructAllocString(path);
				scope_entry->name = StructAllocString(name);
			}
		}

		eaQSortG(def->scope_entries_load, cmpScopeEntries);
	}
}

#define BACKUP_TIME_TO_KEEP (60*60*24*14) //	 in seconds

static int defCmp(const GroupDef **defAPtr, const GroupDef **defBPtr)
{
	const GroupDef *defA = *defAPtr;
	const GroupDef *defB = *defBPtr;

	if (defA == defB)
		return 0;
	if (!defA)
		return -1;
	if (!defB)
		return 1;
	return defA->name_uid - defB->name_uid;
}

static int writeToDisk(const char *fname, LibFileLoad *lib)
{
	int ret;
// 	__time32_t save_time, cur_time;

	eaQSortG(lib->defs, defCmp);

	if( !isProductionEditMode() ) { 
		fileMakeLocalBackup(fname, BACKUP_TIME_TO_KEEP);
	}
	ret = ParserWriteTextFile(fname, parse_LibFileLoad, lib, 0, 0);
	if (!ret)
	{
		char buf[2048];
		sprintf(buf,"Failed to save %s.  lastWinErr()=%s",fname,lastWinErr());
		buf[strlen(buf)-2]=0;	// trims crap off end of lastWinErr()
		log_printf(LOG_GROUPSAVE,"%s",buf);
		assertmsg(0, buf);
	}

	// CD: windows doesn't give us a folder cache update if the new file is the same is the old, so this check becomes invalid

// 	save_time = fileLastChanged(fname);
// 	cur_time = time(NULL);
// 	if (save_time+30<cur_time)
// 	{
// 		char buf[2048];
// 		sprintf(buf,"Failed to save %s.  Timestamp did not change after saving.",fname);
// 		log_printf("groupsave.log","%s",buf);
// 		assertmsg(0, buf);
// 	}

	return ret;
}


////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

int worldSaveSimpleDefs(const char *filename, SimpleGroupDef **defs)
{
	int i, j, ret;
	LibFileLoad *lib;

	lib = StructCreate(parse_LibFileLoad);
	lib->version = 1;

	for (i = 0; i < eaSize(&defs); ++i)
	{
		SimpleGroupDef *def = defs[i];
		GroupDef *defload = StructCreate(parse_GroupDef);
		defload->name_uid = def->name_uid;
		defload->name_str = allocAddString(def->name_str);

		if (def->modelname[0])
			defload->model_name = StructAllocString(def->modelname);

		for (j = 0; j < eaSize(&def->children); ++j)
		{
			SimpleGroupChild *ent = def->children[j];
			GroupChild *groupload = StructAlloc(parse_GroupChild);
			groupload->name_uid = ent->def->name_uid;
			groupload->name = ent->def->name_str;
			extractMat(groupload, ent->mat);
			groupload->seed = ent->seed;
			eaPush(&defload->children, groupload);
		}

		eaPush(&lib->defs, defload);
	}

	ret = writeToDisk(filename, lib);
	StructDestroy(parse_LibFileLoad, lib);

	return ret;
}

static bool groupDefIsSaved(const GroupDef* def)
{
	return def->referenced && !def->is_dynamic && def->name_uid && def->name_str[0] != '^';
}

static void groupMarkReferencedRecursive(GroupDef *def)
{
	int i;
	if (!def || groupIsObjLib(def))
		return;
	def->referenced = 1;
	for (i = 0; i < eaSize(&def->children); i++)
	{
		GroupDef *child_def = groupChildGetDef(def, def->children[i], false);
		if (child_def)
			groupMarkReferencedRecursive(child_def);
	}
}

int saveGroupFileAs(LibFileLoad *lib, GroupDefLib *def_lib, const char *filename)
{
	GroupDef **lib_defs = groupLibGetDefEArray(def_lib);
	int		i, size;
	bool	is_rootmods_file = false;
	char	buf[1000];
	LibFileLoad *modelnames = NULL;
	bool	is_core;
	GroupDef *root_def = NULL;
	const char *set_filename = NULL;
	bool	is_autosave = true;

	for (i = 0; i < eaSize(&lib_defs); i++)
	{
		GroupDef *def = lib_defs[i];
		if (def)
		{
			def->referenced = false;
			if (def->name_uid == 1)
			{
				root_def = def;
			}
		}
	}

	assert(root_def);
	groupMarkReferencedRecursive(root_def);

	world_grid.loading++;

	if (!strEndsWith(filename, ".autosave"))
	{
		int it;
		GroupDefPropertyGroup gdg;
		gdg.filename = filename;
		gdg.props = NULL;

		lib_defs = groupLibGetDefEArray(def_lib);
		for( it = 0; it != eaSize( &lib_defs ); ++it ) {
			if( groupDefIsSaved( lib_defs[it] )) {
				eaPush( &gdg.props, &lib_defs[it]->property_structs );
			}
		}
		
		langApplyEditorCopySingleFile(parse_GroupDefPropertyGroup, &gdg, true, false);
		eaDestroy( &gdg.props );

		is_autosave = false;
	}

	if (!lib)
		lib = StructCreate(parse_LibFileLoad);
	lib->version = 1;

	lib_defs = groupLibGetDefEArray(def_lib);
	for (i = 0; i < eaSize(&lib_defs); i++)
	{
		// save all defs that are not journal entries or dynamic groups
		if (lib_defs[i] && groupDefIsSaved(lib_defs[i]))
		{
			groupFixupBeforeWrite(lib_defs[i], true);
			eaPush(&lib->defs, lib_defs[i]);
		}
	}

	world_grid.loading--;

	fileLocateWrite(filename, buf);
	is_core = strStartsWith(buf, fileCoreDataDir());
	if (is_core)
		strstriReplace(buf, fileDataDir(), fileCoreDataDir());

	if (is_autosave)
		set_filename = lib_defs[0]->filename;
	else
	{
		char rel_path[MAX_PATH];
		if (def_lib->zmap_layer)
			strcpy(rel_path, def_lib->zmap_layer->filename);
		else
			fileRelativePath(buf, rel_path);
		set_filename = allocAddFilename(rel_path);
	}

	mkdirtree(buf);
	if (fileIsReadOnly(buf))
	{
		ErrorFilenamef(buf, "File cannot be saved! It is not currently checked out!");
		return 0;
	}
	writeToDisk(buf, lib);
	for (i = 0; i < eaSize(&lib_defs); i++)
	{
		lib_defs[i]->filename = set_filename;
	}
	eaDestroy(&lib->defs);
	StructDestroy(parse_LibFileLoad, lib);
	
	groupLibMarkBadChildren(def_lib);
	groupLibConsistencyCheckAll(def_lib, true);

	size = fileSize(buf);
	return size;
}
