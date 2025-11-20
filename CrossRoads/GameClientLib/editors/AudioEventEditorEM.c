GCC_SYSTEM
#ifndef NO_EDITORS

#include "AudioEventEditor.h"
#include "estring.h"
#include "fileutil.h"
#include "Sound_common.h"
#include "Color.h"

// For CreateColorRGB

// For world*PublicName calls
#include "WorldGrid.h"
#include "AutoGen/AudioEventEditorEM_c_ast.h"
#include "AutoGen/Sound_common_h_ast.h"

EMEditor gAudioEventEditor;
EMPicker gAudioEventPicker;

#endif

// Need to have all AUTO_STRUCTs present even if NO_EDITORS to make things compile

typedef struct DirectoryFolder DirectoryFolder;
static void addFileToPicker(const char *filename);

AUTO_STRUCT;
typedef struct DirectoryEntry {
	char *entry_filename;
	char *entry_name;				
} DirectoryEntry;

AUTO_STRUCT;
typedef struct DirectoryFolder {
	char *folder_name;

	DirectoryFolder **folders;		
	DirectoryEntry **entries;	
} DirectoryFolder;

AUTO_STRUCT;
typedef struct AudioEventPickerList {
	const char *top_level_name;  NO_AST

	DirectoryFolder **root_folders;
} AudioEventPickerList;

#ifndef NO_EDITORS

AudioEventPickerList g_picker_list;

char **g_picker_files = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void audioEventEditorEMInit(EMEditor *pEditor)
{
	// Set up the data
	audioEventEdit_InitData(pEditor);
}

static char *strStripPathAndExt(const char *str)
{
	char *name = NULL;
	char *ext = NULL;

	name = strdup(strrchr(str, '/')+1);
	if(!name) return NULL;
	ext = strchr(name, '.');
	if(!ext) return NULL;
	ext[0] = '\0';

	return name;
}

static char *strStripExt(const char *str)
{
	static char name[MAX_PATH] = {0};
	char *file = NULL;

	strcpy(name, str);
	file = strrchr(name,'/');

	if(file)
	{
		file[0] = '\0';
	}
	else
	{
		return NULL;
	}
	
	return name;
}

static EMEditorDoc *audioEventEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	AudioEventEditDoc *pDoc;
	GameAudioEventMap *map = NULL;
	char path[MAX_PATH] = {0};

	strcpy(path, pcName);
	getDirectoryName(path);
	
	sndCommonLoadToGAEDict(path);

	map = RefSystem_ReferentFromString(g_GAEMapDict, pcName);

	if(map)
	{
		pDoc = audioEventEdit_OpenAudioEvent(&gAudioEventEditor, map);

		return &pDoc->emDoc;
	}

	return NULL;
}

static EMEditorDoc *audioEventEditorEMNewDoc(const char *pcType, void *data)
{
	assert(data);		// MAke sure custom new was applied
	return (EMEditorDoc*)audioEventEdit_OpenAudioEvent(&gAudioEventEditor, (GameAudioEventMap*)data);
}

static void audioEventEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	audioEventEdit_CloseAudioEvent((AudioEventEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}

// typedef bool (*EMEditorCloseDocCheckFunc)(EMEditorDoc *doc, bool quitting);
static bool audioEventEditorEMCloseCheck(EMEditorDoc *doc, bool quitting)
{
	return audioEventEdit_CloseCheck((AudioEventEditDoc*)doc);
}

static EMTaskStatus audioEventEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	if (audioEventEdit_SaveAudioEvent((AudioEventEditDoc*)pDoc, false))
		return EM_TASK_SUCCEEDED;
	return EM_TASK_FAILED;
}

static EMTaskStatus audioEventEditorEMSaveAsDoc(EMEditorDoc *pDoc)
{
	if (audioEventEdit_SaveAudioEvent((AudioEventEditDoc*)pDoc, true))
		return EM_TASK_SUCCEEDED;
	return EM_TASK_FAILED;
}

// Picker details

//typedef void (*EMPickerTreeNodeSelectedFunc)(EMPicker *browser, EMPickerSelection *selection);
bool audioEventEditorEMMapSelected(EMPicker *picker, EMPickerSelection *selection)
{
	const char *zonedir = NULL;
	DirectoryEntry *entry = (DirectoryEntry*)selection->data;

	strcpy(selection->doc_type, AUDIO_EVENT);
	strcpy(selection->doc_name, entry->entry_filename);

	return true;
}

// typedef FileScanAction (*FileScanProcessor)(char* dir, struct _finddata32_t* data, void *pUserData);
FileScanAction audioEventEditorEMPickerFindFiles(char *dir, struct _finddata32_t* data, void *pUserData)
{
	if(data->attrib & _A_SUBDIR)
	{
		return FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		if(strEndsWith(data->name, ".gaelayer"))
		{
			char *str = NULL;
			estrPrintf(&str, "%s/%s", dir, data->name);
			eaPush(&g_picker_files, str);
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}

void audioEventEditorGAELayersChanged(const char *relpath, int when, void *userData)
{
	EMPicker *pPicker = (EMPicker*)userData;

	// TODO: need to clear the picker
	if(strEndsWith(relpath, ".gaelayer"))
		addFileToPicker(relpath);
}

static void audioEventEditorEMPickerInit(EMPicker *pPicker)
{
	EMPickerDisplayType *encDispType;

	// Load initial data, cheating way
	// TODO: Make this use server/client editing
	fileScanAllDataDirs("maps/", audioEventEditorEMPickerFindFiles, NULL);
	fileScanAllDataDirs("sound/", audioEventEditorEMPickerFindFiles, NULL);

	sndCommonAddChangedCallback(audioEventEditorGAELayersChanged, pPicker);

	// Set up the list
	pPicker->display_parse_info_root = parse_AudioEventPickerList;
	pPicker->display_data_root = &g_picker_list;

	encDispType = calloc(1, sizeof(EMPickerDisplayType));
	encDispType->parse_info = parse_DirectoryEntry;
	encDispType->display_name_parse_field = "Entry_Name";
	encDispType->color = CreateColorRGB(0, 0, 0);
	encDispType->selected_color = CreateColorRGB(255, 255, 255);
	encDispType->is_leaf = 1;
	encDispType->selected_func = audioEventEditorEMMapSelected;
	eaPush(&pPicker->display_types, encDispType);

	encDispType = calloc(1, sizeof(EMPickerDisplayType));
	encDispType->parse_info = parse_DirectoryFolder;
	encDispType->display_name_parse_field = "Folder_Name";
	encDispType->color = CreateColorRGB(0, 0, 0);
	encDispType->selected_color = CreateColorRGB(255, 255, 255);
	encDispType->is_leaf = 0;
	//encDispType->selected_func = audioEventEditorEMMapSelected;
	eaPush(&pPicker->display_types, encDispType);
}

static void audioEventEditorEMFreePickerList(GameAudioEventMap *map)
{
	StructDestroy(parse_GameAudioEventMap, map);
}

static void strSplit(const char *filename, char ***arr, char splitby)
{
	char *prev_loc;
	char *loc;
	char dupe[MAX_PATH];

	strcpy(dupe, filename);

	prev_loc = &dupe[0];
	loc = strchr(dupe, splitby);

	while(loc || prev_loc)
	{
		if(loc)
		{
			loc[0] = '\0';
			loc++;
		}

		eaPush(arr, strdup(prev_loc));
		
		prev_loc = loc;
		if(loc)
		{
			loc = strchr(loc, splitby);
		}
	}
}

DirectoryFolder *findFolderInList(DirectoryFolder ***folderlist, const char *foldername)
{
	int i;

	for(i=0; i<eaSize(folderlist); i++)
	{
		DirectoryFolder *folder = (*folderlist)[i];

		if(!stricmp(folder->folder_name, foldername))
		{
			return folder;
		}
	}

	return NULL;
}

static void addFileToPicker(const char *filename)
{
	int i;
	char **arr = NULL;
	DirectoryFolder *last_folder = NULL;
	DirectoryEntry *entry = NULL;
	char *name = NULL;
	char *ext = NULL;

	name = strdup(strrchr(filename, '/')+1);
	if(!name)
	{
		return;
	}

	ext = strchr(name, '.');
	if(!ext)
	{
		return;
	}

	ext[0] = '\0';

	strSplit(filename, &arr, '/');

	for(i=0; i<eaSize(&arr)-1; i++)
	{
		DirectoryFolder ***folderlist = last_folder ? &last_folder->folders : &g_picker_list.root_folders;
		DirectoryFolder *folder = findFolderInList(folderlist, arr[i]);

		if(!folder)
		{
			folder = StructCreate(parse_DirectoryFolder);

			folder->folder_name = strdup(arr[i]);

			eaPush(folderlist, folder);
		}

		last_folder = folder;
	}

	assert(last_folder);

	FOR_EACH_IN_EARRAY(last_folder->entries, DirectoryEntry, entryTest)
	{
		if(!stricmp(entryTest->entry_filename, filename))
			entry = entryTest;
	}
	FOR_EACH_END;

	if(!entry)
	{
		entry = callocStruct(DirectoryEntry);
		entry->entry_filename = strdup(filename);
		eaPush(&last_folder->entries, entry);
	}
	
	SAFE_FREE(entry->entry_name);
	entry->entry_name = strdup(name);

	eaDestroyEx(&arr, NULL);
}

static void audioEventEditorEMPickerLoadMapList(void)
{
	int i;
	
	for(i=0; i<eaSize(&g_picker_files); i++)
	{
		char *file = g_picker_files[i];

		addFileToPicker(file);
	}
}

static void audioEventEditorEMPickerEnter(EMPicker *pPicker)
{
	devassertmsg(pPicker==&gAudioEventPicker, "Invalid picker passed into audio picker enter");

	audioEventEditorEMPickerLoadMapList();
	emPickerRefresh(&gAudioEventPicker);
}	


static void audioEventEditorEMPickerLeave(EMPicker *pPicker)
{
	eaDestroyStruct(&g_picker_list.root_folders, parse_DirectoryFolder);
	g_picker_list.root_folders = NULL;
}



//typedef void (*UICancelFunc) (UserData);
void audioEventEditorEM_CancelCustomNew(UserData data)
{
	// nothing to do here
}

//typedef void (*UIFileSelectFunc) (const char *, const char *, UserData);
void audioEventEditorEM_CustomOk(const char *path, const char *filename, UserData data)
{
	GameAudioEventMap *map = StructCreate(parse_GameAudioEventMap);

	sprintf(map->zone_dir, "%s/%s", path, filename);

	emNewDoc(AUDIO_EVENT, map);
}


void audioEventEditorEM_createLocalMap(UIAnyWidget *widget, UserData userData)
{
	UIWindow *browser;
	UIWindow *srcWindow = (UIWindow*)userData;
	ui_WindowClose(srcWindow);

	browser = ui_FileBrowserCreate("Select Zone file", "New", UIBrowseExisting, UIBrowseFiles, false, "maps", "maps", NULL, 
		".zone", audioEventEditorEM_CancelCustomNew, NULL, audioEventEditorEM_CustomOk, NULL);
	ui_WindowShow(browser);

}

void audioEventEditorEM_CancelNewGlobalMap(UserData data)
{
	// nothing to do here
}

void audioEventEditorEM_GlobalMapOk(const char *path, const char *filename, UserData data)
{
	GameAudioEventMap *map = StructCreate(parse_GameAudioEventMap);
	sprintf(map->zone_dir, "%s/%s", path, filename);
	emNewDoc(AUDIO_EVENT, map);
}

void audioEventEditorEM_createGlobalMap(UIAnyWidget *widget, UserData userData)
{
	UIWindow *browser;
	UIWindow *srcWindow = (UIWindow*)userData;
	
	ui_WindowClose(srcWindow);

	browser = ui_FileBrowserCreate("Select Folder", "Choose", UIBrowseNewOrExisting, UIBrowseFolders, false, "sound/gaelayers", "sound/gaelayers", NULL, 
		"", audioEventEditorEM_CancelNewGlobalMap, NULL, audioEventEditorEM_GlobalMapOk, NULL);
	ui_WindowShow(browser);
}

//typedef void (*EMEditorDocCustomNewFunc)(void);
void audioEventEditorEMGetZone(void)
{
	UIWindow *pWin;
	UIButton *pButton;

	pWin = ui_WindowCreate("Create Global or Local Map", 200, 200, 250, 200);

	pButton = ui_ButtonCreate("Create New Local Map", 0, 0, audioEventEditorEM_createLocalMap, pWin);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UITopLeft);
	ui_WindowAddChild(pWin, pButton);

	pButton = ui_ButtonCreate("Create New Global Map", 0, 0, audioEventEditorEM_createGlobalMap, pWin);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 30, 0, 0, UITopLeft);
	ui_WindowAddChild(pWin, pButton);
	
	ui_WindowShow(pWin);
}


#endif

AUTO_RUN;
int audioEventEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gAudioEventEditor.editor_name, AUDIO_EVENT_EDITOR);
	gAudioEventEditor.type = EM_TYPE_SINGLEDOC;//EM_TYPE_MULTIDOC;
	gAudioEventEditor.hide_world = 0;
	gAudioEventEditor.disable_single_doc_menus = 1;
	gAudioEventEditor.disable_auto_checkout = 1;
	gAudioEventEditor.default_type = AUDIO_EVENT;
	strcpy(gAudioEventEditor.default_workspace, "Audio Editors");

	gAudioEventEditor.init_func = audioEventEditorEMInit;
	gAudioEventEditor.new_func = audioEventEditorEMNewDoc;
	gAudioEventEditor.custom_new_func = audioEventEditorEMGetZone;
	gAudioEventEditor.load_func = audioEventEditorEMLoadDoc;
	gAudioEventEditor.save_func = audioEventEditorEMSaveDoc;
	gAudioEventEditor.save_as_func = audioEventEditorEMSaveAsDoc;
	gAudioEventEditor.close_func = audioEventEditorEMCloseDoc;
	gAudioEventEditor.close_check_func = audioEventEditorEMCloseCheck;

	// Register the picker
	gAudioEventPicker.allow_outsource = 1;
	strcpy(gAudioEventPicker.picker_name, "Audio Event Library");
	gAudioEventPicker.init_func = audioEventEditorEMPickerInit;
	gAudioEventPicker.enter_func = audioEventEditorEMPickerEnter;
	gAudioEventPicker.leave_func = audioEventEditorEMPickerLeave;
	strcpy(gAudioEventPicker.default_type, AUDIO_EVENT);
	eaPush(&gAudioEventEditor.pickers, &gAudioEventPicker);

	emRegisterEditor(&gAudioEventEditor);
	emRegisterFileType(AUDIO_EVENT, AUDIO_EVENT, AUDIO_EVENT_EDITOR);
#endif

	return 0;
}

#include "autogen/audioeventeditorem_c_ast.c"

