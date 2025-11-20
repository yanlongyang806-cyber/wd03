GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "FolderCache.h"
#include "dynFxInfo.h"
#include "StringCache.h"
#include "Color.h"
#include "FXBrowser_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//////////////////////////////////////////////////////////////////////////
// FX Library browser

#endif
typedef struct FXLibraryFolder FXLibraryFolder;

AUTO_STRUCT;
typedef struct FXLibraryFile
{
	const char *name;				AST( POOL_STRING )
	const char *filename;			AST( POOL_STRING FILENAME )
	bool bParticle;					NO_AST
} FXLibraryFile;

AUTO_STRUCT;
typedef struct FXLibraryFolder
{
	const char *name;				AST( POOL_STRING )
	FXLibraryFolder **folders;
	FXLibraryFile **files;
} FXLibraryFolder;
#ifndef NO_EDITORS

static EMPicker fx_browser; // particles
static EMPicker fxinfo_browser; // fx
static FXLibraryFolder fx_root;

ParseTable parse_FXLibraryFile[];
ParseTable parse_FXLibraryFolder[];

static void fxbInitLeaf(FXLibraryFile *file, FolderNode *node, bool bParticle)
{
	char filename[MAX_PATH];
	char fxname[MAX_PATH];
	getFileNameNoExt(fxname, node->name);
	file->bParticle = bParticle;

	FolderNodeGetFullPath(node, filename);
	file->filename = allocAddFilename(filename);
	file->name = node->name;
}

static void fxbAddFXToTreeRecurse(FXLibraryFolder *root, FolderNode *node)
{
	// Add all of node->contents into root->folders and root->files
	FolderNode *walk = node->contents;

	while (walk) {
		if (walk->is_dir) {
			FXLibraryFolder *folder=NULL;
			folder = StructAlloc(parse_FXLibraryFolder);
			folder->name = allocAddString(walk->name);
			eaPush(&root->folders, folder);
			fxbAddFXToTreeRecurse(folder, walk);
		}
		else if (strEndsWith(walk->name, ".part"))
		{
			FXLibraryFile *file = StructAlloc(parse_FXLibraryFile);
			fxbInitLeaf(file, walk, strEndsWith(walk->name, ".part"));
			eaPush(&root->files, file);
		}
		walk = walk->next;
	}
// 	// Sort files and folders
// 	eaQSort(root->files, cmpFXLibraryFile);
// 	eaQSort(root->folders, cmpFXLibraryFolder);
}

static void fxbAddFXToTree(void)
{
	FolderNode *node;
	
	StructDeInit(parse_FXLibraryFolder, &fx_root);
	fx_root.name = allocAddString("FX");
	
	FolderCacheRequestTree(folder_cache, "dyn/fx"); // If we're in dynamic mode, this will load the tree!
	node = FolderNodeFind(folder_cache->root, "dyn/fx");
	if (!node || !node->contents) {
		devassert(0);
		return; // Empty?
	}
	fxbAddFXToTreeRecurse(&fx_root, node);
}

static bool fxbFXSelectedFunc(EMPicker *picker, EMPickerSelection *selection)
{
	FXLibraryFile *fxb_entry = selection->data;

	assert(fxb_entry);
	strcpy(selection->doc_name, fxb_entry->filename);
	if (fxb_entry->bParticle)
		strcpy(selection->doc_type, "part");
	else
		strcpy(selection->doc_type, "FX");

	return true;
}


static void fxbInit(EMPicker *browser)
{
	EMPickerDisplayType *display_type;

	fxbAddFXToTree();

	browser->display_data_root = &fx_root;
	browser->display_parse_info_root = parse_FXLibraryFolder;

	display_type = calloc(sizeof(*display_type),1);
	display_type->parse_info = parse_FXLibraryFolder;
	display_type->display_name_parse_field = "name";
	display_type->selected_func = NULL;
	display_type->is_leaf = 0;
	display_type->color = CreateColorRGB(0, 0, 0);
	display_type->selected_color = CreateColorRGB(255, 255, 255);
	eaPush(&browser->display_types, display_type);

	display_type = calloc(sizeof(*display_type),1);
	display_type->parse_info = parse_FXLibraryFile;
	display_type->display_name_parse_field = "name";
	display_type->selected_func = fxbFXSelectedFunc;
	display_type->is_leaf = 1;
	display_type->color = CreateColorRGB(0, 0, 80);
	display_type->selected_color = CreateColorRGB(255, 255, 255);
	eaPush(&browser->display_types, display_type);

//	eaPush(&browser->new_types, "Fx");
//	eaPush(&browser->new_types, "Particle");
}

void fxbNeedsRebuild(void)
{
	if (fx_root.folders) {
		fxbAddFXToTree();
		emPickerRefresh(&fx_browser);
	}
}

static void fxbNeedsRebuildCallback(const char *relpath, int when)
{
	fxbNeedsRebuild();
}

void fxbRegister(EMEditor *editor)
{
	strcpy(fx_browser.picker_name, "Particle Library");
	fx_browser.init_func = fxbInit;
	fx_browser.allow_outsource = 1;
	strcpy(fx_browser.default_type, "part");
//	eaPush(&fx_browser.gimme_groups, "Art");
//	eaPush(&fx_browser.gimme_groups, "FX");
//	eaPush(&fx_browser.gimme_groups, "Software");
//	emPickerRefresh(&fx_browser);

	eaPush(&editor->pickers, &fx_browser);

	fxinfo_browser.allow_outsource = 1;
	strcpy(fxinfo_browser.picker_name, "FX Library");
	strcpy(fxinfo_browser.default_type, "DynFxInfo");
	emPickerManage(&fxinfo_browser);
	emPickerRegister(&fxinfo_browser);

	dynFxInfoAddReloadCallback(fxbNeedsRebuildCallback);
}

#endif

#include "AutoGen/FXBrowser_c_ast.c"