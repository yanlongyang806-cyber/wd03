/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/


#include "dynSkeleton.h"
#include "CostumeCommonLoad.h"
#include "error.h"
#include "fileutil2.h"
#include "qsortG.h"
#include "wlModel.h"



AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// --------------------------------------------------------------------------

bool bDoSkelFixup;

// automatically generates skel_fixup.info files from costume data
AUTO_CMD_INT(bDoSkelFixup, doSkelFixup);

// the list of skeletons to fixup
static const char *fixup_skeletons[] = 
{
	"NoFixup", // MUST be first!
	"Core_4Arms_Male/Skel_Core_4Arms_Male",
	"Core_ApeBot/Skel_ApeBot",
	"Core_Bear/Skel_core_bear",
	"Core_Female/Skel_Core_Female",
	"Core_Quad/Skel_Core_Quad",
	"Core_Raptor/Raptor_skel",
	"Core_STO_Male/Skel_Core_STO_Male",
	"CoreDefault/Skel_Core_Default",
	"Fem/Skel_Fem",
	"Bear/Skel_Bear",
	"Female/Skel_female",
	"Male/Skel_Core_Default",
	"Raptor/Skel_Raptor"
};

typedef struct FixupFileData
{
	char *geo_name;
	int skeleton;
} FixupFileData;

typedef struct FixupDirectoryData
{
	char *dir;
	FixupFileData **files;
} FixupDirectoryData;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct SkeletonFixupOverride
{
	char *vrml_filename;				AST( STRUCTPARAM )
	char *skeleton_name;				AST( STRUCTPARAM )
} SkeletonFixupOverride;

AUTO_STRUCT;
typedef struct SkeletonFixupInfo
{
	char *skeleton_name;				AST( NAME(Skeleton) )
	SkeletonFixupOverride **overrides;	AST( NAME(Override) )
} SkeletonFixupInfo;

AUTO_STRUCT;
typedef struct CBoneFileList
{
	char **files;			AST( NAME(Filename) )
} CBoneFileList;

#include "SkelInfoFixup_c_ast.c"

static void freeFixupFileData(FixupFileData *data)
{
	SAFE_FREE(data->geo_name);
	free(data);
}

static void freeFixupDirectoryData(FixupDirectoryData *data)
{
	eaDestroyEx(&data->files, freeFixupFileData);
	SAFE_FREE(data->dir);
	free(data);
}

static void dataToSrc(char *in_name,char *out_name, size_t out_name_size)
{
	const char *s;

	s = strstri(in_name,"/data/");
	if (s) {
		s += strlen("/data/");
	} else {
		s = strstri(in_name,"/CoreData/");
		assert(s);  // One of either data or CoreData must exist
		s += strlen("/CoreData/");
	}

	if (strStartsWith(in_name, fileCoreDataDir()))
		strcpy_s(out_name, out_name_size, fileCoreSrcDir());
	else
		strcpy_s(out_name, out_name_size, fileSrcDir());
	strcatf_s(out_name, out_name_size, "/%s", s);
	forwardSlashes(out_name);
}

static int cmpOverride(const SkeletonFixupOverride **po1, const SkeletonFixupOverride **po2)
{
	return stricmp((*po1)->vrml_filename, (*po2)->vrml_filename);
}

static void addCostumeToFixupHashes(PlayerCostume *costume, int skeleton, StashTable cbone_filenames, StashTable fixup_directories, const char *actual_skeleton_name)
{
	int i, j;

	for (i = 0; i < eaSize(&costume->eaParts); ++i)
	{
		PCPart *costume_part = costume->eaParts[i];
		PCGeometryDef *geo = GET_REF(costume_part->hGeoDef);
		PCBoneDef *bone = GET_REF(costume_part->hBoneDef);
		const char *geo_name;
		char geo_path[MAX_PATH], *geo_filename;
		FixupDirectoryData *dir_data;
		FixupFileData *file_data;
		ModelHeaderSet *mset;

		if (bone && cbone_filenames)
			stashAddPointer(cbone_filenames, bone->pcFileName, bone->pcFileName, false);
	
		if (!geo || !geo->pcGeometry)
			continue;

		bone = GET_REF(geo->hBone);
		if (bone && cbone_filenames)
			stashAddPointer(cbone_filenames, bone->pcFileName, bone->pcFileName, false);

		mset = modelHeaderSetFind(geo->pcGeometry);
		if (!mset)
			continue;
		geo_name = mset->filename;
		if (!geo_name)
			continue;

		fileLocateWrite(geo_name, geo_path);
		geo_filename = getFileName(geo_path);
		if (geo_filename > geo_path)
			geo_filename[-1] = 0;

		if (!stashFindPointer(fixup_directories, geo_path, &dir_data))
		{
			dir_data = calloc(1, sizeof(FixupDirectoryData));
			dir_data->dir = strdup(geo_path);
			stashAddPointer(fixup_directories, geo_path, dir_data, true);
		}

		for (j = 0; j < eaSize(&dir_data->files); ++j)
		{
			if (stricmp(dir_data->files[j]->geo_name, geo_filename) == 0)
			{
				if (skeleton != dir_data->files[j]->skeleton)
				{
					if (skeleton == 0 || dir_data->files[j]->skeleton == 0)
						printf("Geometry file %s used by more than one skeleton, and one of them is not being fixed up!\n", geo_filename);
					else
						verbose_printf("Geometry file %s used by more than one skeleton!\n", geo_filename);

					// use the highest priority skeleton
					if (dir_data->files[j]->skeleton == 0)
						dir_data->files[j]->skeleton = skeleton;
					else if (skeleton != 0)
						dir_data->files[j]->skeleton = MIN(dir_data->files[j]->skeleton, skeleton);
				}
				break;
			}
		}

		if (j == eaSize(&dir_data->files))
		{
			file_data = calloc(1, sizeof(FixupFileData));
			file_data->geo_name = strdup(geo_filename);
			file_data->skeleton = skeleton;
			eaPush(&dir_data->files, file_data);
		}
	}
}


static void generateSkelFixupInfosInternal(void)
{
	// this code generates skel_fixup.info files automatically from costume files
	RefDictIterator iterator;
	PlayerCostume *costume;
	int counts[ARRAY_SIZE(fixup_skeletons)];
	StashTable cbone_filenames = stashTableCreateWithStringKeys(128, StashDefault);
	StashTable fixup_directories = stashTableCreateWithStringKeys(256, StashDeepCopyKeys);
	CBoneFileList cbone_file_list = {0};
	StashTableIterator iter;
	StashElement elem;
	int i;

	STATIC_INFUNC_ASSERT(ARRAY_SIZE(counts) == ARRAY_SIZE(fixup_skeletons));

	RefSystem_InitRefDictIterator(g_hPlayerCostumeDict, &iterator);
	while ((costume = RefSystem_GetNextReferentFromIterator(&iterator)))
	{
		PCSkeletonDef *skeleton_def = GET_REF(costume->hSkeleton);
		const DynBaseSkeleton *base_skeleton;
		SkelInfo *skel;

		if (!skeleton_def)
			continue;

		skel = RefSystem_ReferentFromString("SkelInfo", skeleton_def->pcSkeleton);
		if (!skel)
			continue;

		base_skeleton = GET_REF(skel->hBaseSkeleton);
		if (!base_skeleton)
			continue;

		for (i = 1; i < ARRAY_SIZE(fixup_skeletons); ++i)
		{
			if (stricmp(fixup_skeletons[i], base_skeleton->pcName)==0)
			{
				addCostumeToFixupHashes(costume, i, cbone_filenames, fixup_directories, base_skeleton->pcName);
				break;
			}
		}
		if (i == ARRAY_SIZE(fixup_skeletons))
			addCostumeToFixupHashes(costume, 0, NULL, fixup_directories, base_skeleton->pcName);
	}

	stashGetIterator(cbone_filenames, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		char *cbone_file = stashElementGetStringKey(elem);
		if (cbone_file)
		{
			char filename[MAX_PATH];
			fileLocateWrite(cbone_file, filename);
			eaPush(&cbone_file_list.files, StructAllocString(filename));
		}
	}

	eaQSortG(cbone_file_list.files, strCmp);
	ParserWriteTextFile("C:/cbone_fixup.txt", parse_CBoneFileList, &cbone_file_list, 0, 0);
	StructReset(parse_CBoneFileList, &cbone_file_list);

	stashGetIterator(fixup_directories, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		FixupDirectoryData *dir_data = stashElementGetPointer(elem);
		if (dir_data)
		{
			SkeletonFixupInfo fixup_info = {0};
			char filename[MAX_PATH], temp_filename[MAX_PATH];
			int highest_skeleton = 0;

			ZeroArray(counts);

			for (i = 0; i < eaSize(&dir_data->files); ++i)
				counts[dir_data->files[i]->skeleton]++;

			for (i = 1; i < ARRAY_SIZE(counts); ++i)
			{
				if (counts[i] > counts[highest_skeleton])
					highest_skeleton = i;
			}

			// check if fixup skeletons are needed in this directory
			if (highest_skeleton == 0 && counts[0] == eaSize(&dir_data->files))
				continue;

			fixup_info.skeleton_name = StructAllocString(fixup_skeletons[highest_skeleton]);
			for (i = 0; i < eaSize(&dir_data->files); ++i)
			{
				if (dir_data->files[i]->skeleton != highest_skeleton)
				{
					SkeletonFixupOverride *file_override = StructCreate(parse_SkeletonFixupOverride);

					strcpy(filename, getFileName(dir_data->files[i]->geo_name));
					changeFileExt(filename, ".wrl", filename);

					file_override->skeleton_name = StructAllocString(fixup_skeletons[dir_data->files[i]->skeleton]);
					file_override->vrml_filename = StructAllocString(filename);
					eaPush(&fixup_info.overrides, file_override);
				}
			}

			eaQSortG(fixup_info.overrides, cmpOverride);

			sprintf(filename, "%s/skel_fixup.info", dir_data->dir);
			fileLocateWrite(filename, temp_filename);
			dataToSrc(temp_filename, SAFESTR(filename));
			ParserWriteTextFile(filename, parse_SkeletonFixupInfo, &fixup_info, 0, 0);
			StructReset(parse_SkeletonFixupInfo, &fixup_info);
		}
	}

	stashTableDestroy(cbone_filenames);
	stashTableDestroyEx(fixup_directories, NULL, freeFixupDirectoryData);
}

void generateSkelFixupInfos(void)
{
	if (bDoSkelFixup)
	{
		loadstart_printf("Generating skeleton fixup files...");
		generateSkelFixupInfosInternal();
		loadend_printf(" done.");
	}
}


