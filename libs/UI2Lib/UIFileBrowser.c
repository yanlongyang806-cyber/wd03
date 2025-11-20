#include "earray.h"
#include "fileutil.h"
#include "Prefs.h"
#include "RenderLib.h"
#include "GfxSpriteText.h"
#include "UILib.h"
#include "textparser.h"
#include "GraphicsLib.h"
#include "EString.h"
#include "ResourceInfo.h"
#include "StringUtil.h"

#define BROWSER_NUM_PREV 5
#define BROWSER_REG_FORMAT "LastBrowsedFolder%i"

// all of the widgets and data we'll need to keep track of
static UIWindow *selectWin, *overwriteWin, *newFolderWin;
static UITextEntry *activeFolder;
static UIComboBox *prevFolders;
static UIList *folderContents;
static UIList *selectedFiles;
static UIComboBox *nameSpaces;
static UITextEntry *nameEntry;
static UITextEntry *newFolderNameEntry;
static UIFileSelectFunc selectFunc;
static UIMultiFileSelectFunc multiSelectFunc;
static UserData selectData;
static UICancelFunc exitFunc;
static UserData exitData;
static UIFilterFunc nameFilterFunc;
static UserData nameFilterData;
static UIBrowserMode browseMode;
static UIBrowserType browseType; 
static char **topmostDirs;
static char activeFolderPath[MAX_PATH];
static char **fileNames;
static char **selectedFileNames;
static char **selectedFilePaths;
static const char **nameSpaceList=NULL;
static char **origTopmostDirs;//Backups for when name space changes the top directories
static char ext[MAX_PATH];
static char **validExts;
static bool noPrivateDirs;
static bool showNameSpaces = true;

#define NO_NAMESPACE_STR "None"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void loadDir(const char *dir, const char* filter, bool shouldSetFilter );

static void getRememberedWidthHeight(int *width, int *height)
{
	char key[1024];
	sprintf(key, "LastBrowsedWH_%s/%s_W", topmostDirs[ 0 ], ext);
	*width = GamePrefGetInt(key, *width);
	sprintf(key, "LastBrowsedWH_%s/%s_H", topmostDirs[ 0 ], ext);
	*height = GamePrefGetInt(key, *height);
}

static void saveRememberedWidthHeight(int width, int height)
{
	char key[1024];
	sprintf(key, "LastBrowsedWH_%s/%s_W", topmostDirs[ 0 ], ext);
	GamePrefStoreInt(key, width);
	sprintf(key, "LastBrowsedWH_%s/%s_H", topmostDirs[ 0 ], ext);
	GamePrefStoreInt(key, height);
}

static void cleanFileName(const char *in, char *out, int bufSize)
{
	strcpy_s(out, bufSize, in);
	removeLeadingAndFollowingSpaces(out);
}

static void upDirectory( void )
{
	// parse the string to find the parent directory
	char *slashLoc = strrchr(activeFolderPath, '/');
	size_t index = (slashLoc ? slashLoc - activeFolderPath : 0);
    
	if (index && index < CRYPTIC_MAX_PATH)
	{
		char newFolder[CRYPTIC_MAX_PATH];
		memcpy(newFolder, activeFolderPath, index);
		newFolder[index] = 0;
		
		// ensure that the user has not traversed past the specified top directory
		if ( strStartsWithAny(newFolder, topmostDirs ))
		{
			loadDir(newFolder, "", true);
		}
        else
		{
			loadDir( topmostDirs[ 0 ], "", true );
		}
	}
	else
	{
		loadDir( topmostDirs[ 0 ], "", true );
	}
}

static void removeAdjacentDuplicates( char** array )
{
    int arraySize = eaSize( &array );
    int writeIt = 1;
    int readIt = 1;

    while( readIt < arraySize ) {
        if( array[ writeIt - 1 ] != NULL && strcmp( array[ writeIt - 1 ], array[ readIt ]) == 0 ) {
            ++readIt;
        } else {
            if( writeIt != readIt ) {
                free( array[ writeIt ]);
                array[ writeIt ] = array[ readIt ];
				array[ readIt ] = NULL;
            }
            ++writeIt;
            ++readIt;
        }
    }

    if( writeIt < arraySize ) {
        int it;

        for( it = writeIt; it < arraySize; ++it ) {
            free( array[ it ]);
        }
        eaSetSize( &array, writeIt );
    }
}

// this is called to free file browser memory
void ui_FileBrowserFree(void)
{
	if (selectWin && selectWin->widget.width > 50 && 
		selectWin->widget.height > 50) // Sanity check
	{
		saveRememberedWidthHeight(selectWin->widget.width, selectWin->widget.height);
	}
	eaDestroyEx(&fileNames, NULL);
	eaDestroyEx(&selectedFileNames, NULL);
	eaDestroyEx(&selectedFilePaths, NULL);
	if (selectWin)
		ui_WidgetQueueFree((UIWidget*)selectWin);
	selectWin = NULL;
}

// folder names are enclosed in angle brackets
static bool isFolder(const char *dispName)
{
	return (dispName[0] == '<');
}

// this is the callback used by the file scanning function, called on each file
// in a directory
static FileScanAction compileFileList(char *dir, struct _finddata32_t *data, void *pUserData)
{
	char fileName[80];
	int offset = 0;

	// add file names to the fileNames earray, enclosing folder names in angle brackets
	if (data->attrib & _A_SUBDIR)
	{
		offset += 2;
	}
	
	sprintf(fileName, "%s%s%s", offset ? "<" : "", data->name, offset ? ">" : "");

	if (!offset && !strEndsWithAny(fileName, validExts))
		return FSA_NO_EXPLORE_DIRECTORY;
	if (noPrivateDirs && data->name[0] == '_')
		return FSA_NO_EXPLORE_DIRECTORY;

    
    {
        char filteredName[ CRYPTIC_MAX_PATH ];

        if( nameFilterFunc ) {
            nameFilterFunc( SAFESTR( filteredName ), fileName, nameFilterData );
        } else {
            strcpy( filteredName, fileName );
        }
        eaPush(&fileNames, strdup(filteredName));
    }

	// tell the scanner not to recurse down subdirectories
	return FSA_NO_EXPLORE_DIRECTORY;
}

// this function is used as a comparator to sort the file names after retrieving them from a directory
static int compareFileNames(const char **a, const char **b)
{
	bool aIsFolder = isFolder(*a);
	bool bIsFolder = isFolder(*b);

	// folders precede files
	if (aIsFolder && !bIsFolder)
	{
		return -1;
	}
	else if (bIsFolder && !aIsFolder)
	{
		return 1;
	}
	else
	{
		return stricmp(*a, *b);
	}
}

/// this function takes care of loading a new directory and setting
/// all of the UI elements as appropriate
static void loadDir(const char *dir, const char* filter, bool shouldSetFilter )
{
	char tempDir[MAX_PATH];
	strcpy(tempDir, dir);
	forwardSlashes(tempDir);

	// validate the directory:
	// 1) exists
	// 2) is in the top directory
	if (dirExistsMultiRoot(tempDir, topmostDirs) && strStartsWithAny( tempDir, topmostDirs ) && (!noPrivateDirs || !strstr(tempDir, "/_")))
	{
		strcpy(activeFolderPath, tempDir);
		eaDestroyEx(&fileNames, NULL);
		eaSetSize(&fileNames, 0);

        fileScanAllDataDirsMultiRoot(dir, compileFileList, topmostDirs, NULL);
        {
            int it = 0;

            while( it != eaSize( &fileNames ))  {
                if( !isWildcardMatch( filter, fileNames[ it ], false, false)) {
                    eaRemoveFast( &fileNames, it );
                } else {
                    ++it;
                }
            }
        }
		eaQSort(fileNames, compareFileNames);
        removeAdjacentDuplicates( fileNames );
		ui_ListSetModel(folderContents, NULL, &fileNames);
		ui_ListSetSelectedRowAndCallback(folderContents, -1);
		ui_ListResetScrollbar(folderContents);

        if( shouldSetFilter ) {
            ui_TextEntrySetTextAndCallback(nameEntry, filter);
        }
	}
	ui_TextEntrySetText(activeFolder, activeFolderPath);
}

// return a full path string given a directory name and a file name
static char *concatDirFile(char *dir, char *file)
{
	char fileName[CRYPTIC_MAX_PATH];

    if( dir[ 0 ] == '\0' ) {
        strcpy( fileName, file );
    } else {
        sprintf(fileName, "%s/%s", dir, file);
    }
	return strdup(fileName);
}

// checks whether the passed-in name is in the list of file names in the current folder
static bool isValidFile(const char *fileName)
{
	int i;
	for(i = 0; i < eaSize(&fileNames); i++)
	{
		if (strcmp(fileName, fileNames[i]) == 0)
		{
			return true;
		}
	}
	return false;
}

// perform necessary functions after selection has successfully occurred
static void postSelect(void)
{
	const char *folder;
	char valueName[128];
	char **folders = NULL;
	int i;
	bool moveDown = true;

	// write last selected folder to the registry
	eaPush(&folders, strdup(activeFolderPath));
	for (i = 0; i < BROWSER_NUM_PREV - 1; i++)
	{
		sprintf(valueName, BROWSER_REG_FORMAT, i + 1);

		if (folder = GamePrefGetString(valueName, NULL))
		{
			int j;
			bool exists = false;
			// check to make sure this string does not already exist in the EArray
			for (j = 0; j < eaSize(&folders); j++)
			{
				exists = exists || (strcmpi(folders[j], folder) == 0);
				if (exists)
				{
					break;
				}
			}
			if (!exists)
			{
				eaPush(&folders, strdup(folder));
			}
		}
	}

	// write the list to the registry
	for (i = 0; i < eaSize(&folders); i++)
	{
		sprintf(valueName, BROWSER_REG_FORMAT, i + 1);

		GamePrefStoreString(valueName, folders[i]);
	}
	ui_FileBrowserFree();
}

// the OK button click callback for the overwrite file dialog
static void overwriteClickOk(UIButton *button, char *fileName)
{
	selectFunc(activeFolderPath, fileName, selectData);
	postSelect();
	ui_WidgetQueueFree((UIWidget*) overwriteWin);
	overwriteWin = NULL;
}

// the Cancel button click callback for the overwrite file dialog
static void overwriteClickCancel(UIButton *button, UserData stuff)
{
	ui_WidgetQueueFree((UIWidget*) overwriteWin);
	overwriteWin = NULL;
}

static bool overwriteCloseWindow(UIAnyWidget *window, UserData stuff)
{
	overwriteClickCancel(NULL, NULL);
	return true;
}

//The filename passed in here is temporary & will be recycled before the OK button is pressed. This means creating a file with a bad name.
static char overwritefileName[CRYPTIC_MAX_PATH];
// create an overwrite file dialog when in BrowseNew mode and the user selects an existing file
static void createOverwriteDialog(char *fileName)
{
	int w, h;
	UIWindow *window = ui_WindowCreate("Overwrite?", 80, 80, 200, 115);
	UILabel *fileLabel;
	UIButton *okButton;
	UIButton *cancelButton = ui_ButtonCreate("Cancel", 10, 10, overwriteClickCancel, NULL);
	strcpy(overwritefileName, fileName);
	fileLabel = ui_LabelCreate(fileName, 10, 26);
	okButton = ui_ButtonCreate("OK", 60, 10, overwriteClickOk, overwritefileName);
	rdrGetDeviceSize(gfxGetActiveOrPrimaryDevice(), NULL, NULL, &w, &h, NULL, NULL, NULL, NULL);
	w -= window->widget.width;
	w /= 2;
	h -= window->widget.height;
	h /= 2;
	window->widget.x = w + 200;
	window->widget.y = h + 200;
	okButton->widget.offsetFrom = cancelButton->widget.offsetFrom = UIBottomRight;
	overwriteWin = window;
	ui_WindowAddChild(window, fileLabel);
	ui_WindowAddChild(window, ui_LabelCreate("The file:", 10, 10));
	ui_WindowAddChild(window, ui_LabelCreate("already exists.  Are you sure you", 10, 42));
	ui_WindowAddChild(window, ui_LabelCreate("want to overwrite this file?", 10, 55));
	ui_WindowAddChild(window, okButton);
	ui_WindowAddChild(window, cancelButton);
	//ui_WindowSetCloseCallback(window, overwriteCloseWindow, NULL);
	ui_WindowSetClosable(window, false); // So that Escape doesn't close both modal windows
	ui_WindowSetModal(window, true);
	window->widget.priority = selectWin->widget.priority + 1;
	ui_WindowPresent(window);
}

// OK button callback
// TODO: this function is simplified; there probably needs to be a lot more filename validation,
// checks for existing files in create/new mode, etc.

static void removeFileFromSelectionList(UIList *uiList, UserData stuff)
{
	int row = ui_ListGetSelectedRow(selectedFiles);

	if (row < eaSize(&selectedFileNames))
	{
		eaRemove(&selectedFileNames, row );
		eaRemove(&selectedFilePaths, row );
	}
	return;
}

static int inverseStrCmp(const char* a, const char* b) {
	return (strcmp(a, b) == 0);
}

static void addFileToSelectionList(UserData selection)
{
	char *storedName = ui_ListGetSelectedObject(folderContents);
	char tempName[MAX_PATH];

	if (browseMode != UIBrowseMultipleExisting)
		return;

	if (!storedName)
	{
		if(eaSize(&fileNames) == 1)
			storedName = fileNames[0];
		else if (ui_TextEntryGetText(nameEntry))
			storedName = (char*)ui_TextEntryGetText(nameEntry);
		else
			return;
	}
	strcpy(tempName, storedName);
	storedName = tempName;

	// go up a dir
	if( strcmp( tempName, ".." ) == 0 || strcmp( tempName, "../" ) == 0 )
	{
		upDirectory();
	}
	// open folders
	if (isFolder(storedName))
	{
		char *fileName;
		int len = UTF8GetLength( storedName );
		char* lastChar = UTF8GetCodepoint( storedName, len - 1 );
		*lastChar = 0;
		fileName = concatDirFile(activeFolderPath, UTF8GetNextCodepoint( storedName ));
		loadDir(fileName, "", true);
		free(fileName);
	}
	else
	{
		if (eaFindCmp(&selectedFileNames, storedName, inverseStrCmp) == -1)
		{
			eaPush(&selectedFileNames, strdup(storedName));
			eaPush(&selectedFilePaths, strdup(activeFolderPath));
			ui_ListSetModel(selectedFiles, NULL, &selectedFileNames);
		}
	}
}
static void okClick(UIWidget *widget, UserData stuff)
{
	char *storedName = ui_ListGetSelectedObject(folderContents);
	char tempName[MAX_PATH];

	if (browseMode == UIBrowseMultipleExisting)
	{
		if (browseType == UIBrowseFiles || browseType == UIBrowseTextureFiles)
		{
			multiSelectFunc(selectedFilePaths, selectedFileNames, selectData);
			postSelect();
			return;
		}
		else
		{
			assert(false);
		}
	}

	if (!storedName)
    {
        if((browseMode == UIBrowseExisting || browseMode == UIBrowseNewOrExisting || browseMode == UIBrowseMultipleExisting) && eaSize(&fileNames) == 1)
            storedName = fileNames[0];
        else if (ui_TextEntryGetText(nameEntry))
            storedName = (char*)ui_TextEntryGetText(nameEntry);
        else
            return;
    }

	strcpy(tempName, storedName);
	storedName = tempName;

    // go up a dir
    if( strcmp( tempName, ".." ) == 0 || strcmp( tempName, "../" ) == 0 )
    {
        upDirectory();
    }
    // open folders
	if (isFolder(storedName))
	{
		char *fileName;
		int len = UTF8GetLength( storedName );
		char* lastChar = UTF8GetCodepoint( storedName, len - 1 );
		*lastChar = 0;
		fileName = concatDirFile(activeFolderPath, UTF8GetNextCodepoint( storedName ));
		loadDir(fileName, "", true);
		free(fileName);
	}
	// file activation = file selection + clicking OK
	else
	{
		bool bActionTaken = false;

        // browse existing files mode
        if ((browseMode == UIBrowseExisting) || (browseMode == UIBrowseNewOrExisting))
        {
            if (browseType == UIBrowseFiles || browseType == UIBrowseTextureFiles)
            {
                if (isValidFile(storedName))
                {
                    selectFunc(activeFolderPath, storedName, selectData);
                    postSelect();
					bActionTaken = true;
                }
            }
            // if browsing folders, return the currently active folder name
            else if (browseType == UIBrowseFolders)
            {
                selectFunc(activeFolderPath, "", selectData);
                postSelect();
				bActionTaken = true;
            }
            else
            {
                assert( false );
            }
        }

        // browse/create new file mode: ensure that the name entry field is not empty
        if (!bActionTaken && (browseMode != UIBrowseExisting))
        {
            char newFileName[CRYPTIC_MAX_PATH];
            cleanFileName(storedName, SAFESTR(newFileName));
            if (nullStr(newFileName))
                return;

            // add the default extension if no extension was specified
            if (strrchr(newFileName, '.') == NULL && !nullStr(ext))
            {
                strcat(newFileName, ext);
            }

            // check to see if the new file name exists already; if so, create an
            // overwrite dialog
            if (isValidFile(newFileName))
            {
				if (browseMode == UIBrowseNew)
					createOverwriteDialog(newFileName);
            }
            else
            {
                selectFunc(activeFolderPath, newFileName, selectData);
                postSelect();
            }
        }		
	}
}

/// filter changed callback.  You can type shell-globbing characters
/// into the name buffer, and the list of files will be filtered
static void filterChanged( UIAnyWidget* ignored1, UserData ignored2 )
{
    const char* filter = ui_TextEntryGetText( nameEntry );
    int numMatchesAccum = 0;

    loadDir( activeFolderPath, filter, false );

	//printf( "Filter changed... new filter=%s num matches=%d\n", filter, numMatchesAccum );
}

// file list entry activation callback
static void fileListActivate(UIList *uiList, UserData stuff)
{
	if (browseMode == UIBrowseMultipleExisting)
		addFileToSelectionList(selectData);
	else
		okClick(NULL, selectData);
}

// file list display callback
static void displayFileName(struct UIList *uiList, struct UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", (char*)(*uiList->peaModel)[index]);
}

// texture list display callback
static void displayTexturePreview(UIList* uiList, UIListColumn* column, S32 row, UserData ignored, char** outTextureName)
{
    char* name;
    
    assert( browseType == UIBrowseTextureFiles );

    name = eaGet(ui_ListGetModel( uiList ), row);
    if( !isFolder( name ))
        estrCopy2( outTextureName, name );
}

// up button callback to traverse upward in the folder hierarchy
static void upClick(UIButton *upButton, UserData stuff)
{
    upDirectory();
}



// the Cancel button click callback for the new folder dialog
static void newFolderClickCancel(UIButton *button, UserData stuff)
{
	// Just free stuff - also called by Ok
	ui_WidgetQueueFree((UIWidget*) newFolderWin);
	newFolderWin = NULL;
}

static bool newFolderCloseWindow(UIAnyWidget *widget, UserData stuff)
{
	newFolderClickCancel(NULL, NULL);
	return true;
}

// the OK button click callback for the new folder dialog
static void newFolderClickOk(UIButton *button, UserData stuff)
{
	const char *newFolderName;
	// Get folder name
	if (newFolderName = ui_TextEntryGetText(newFolderNameEntry))
	{
		char fullpath[MAX_PATH];
		sprintf(fullpath, "%s/%s", activeFolderPath, newFolderName);
		if (dirExists(fullpath) || fileExists(fullpath)) {
			Alertf("A file or folder named %s already exists!", fullpath);
		} else {
			char onDiskPath[MAX_PATH];
			fileLocateWrite(fullpath, onDiskPath);
			makeDirectories(onDiskPath);
			loadDir(activeFolderPath, "", false);
		}
	}

	// Free stuff
	newFolderClickCancel(button, stuff);
}

static void newClick(UIButton *upButton, UserData stuff)
{
	int w, h;
	UILabel *namePrompt;
	UIWindow *window = ui_WindowCreate("New Folder", 80, 80, 300, 64);
	UIButton *okButton = ui_ButtonCreate("OK", 60, 10, newFolderClickOk, NULL);
	UIButton *cancelButton = ui_ButtonCreate("Cancel", 10, 10, newFolderClickCancel, NULL);

	newFolderNameEntry = ui_TextEntryCreate("", 10, 40);
	newFolderNameEntry->widget.width = 1;
	newFolderNameEntry->widget.leftPad = 50;
	newFolderNameEntry->widget.rightPad = 10;
	newFolderNameEntry->widget.offsetFrom = UIBottomLeft;
	newFolderNameEntry->widget.widthUnit = UIUnitPercentage;
	ui_TextEntrySetEnterCallback(newFolderNameEntry, newFolderClickOk, NULL);

	rdrGetDeviceSize(gfxGetActiveOrPrimaryDevice(), NULL, NULL, &w, &h, NULL, NULL, NULL, NULL);
	w -= window->widget.width;
	w /= 2;
	h -= window->widget.height;
	h /= 2;
	window->widget.x = w + 200;
	window->widget.y = h + 200;
	okButton->widget.offsetFrom = cancelButton->widget.offsetFrom = UIBottomRight;
	namePrompt = ui_LabelCreate("Name: ", 10, 40);
	namePrompt->widget.offsetFrom = UIBottomLeft;
	ui_WindowAddChild(window, namePrompt);
	ui_WindowAddChild(window, newFolderNameEntry);
	ui_WindowAddChild(window, okButton);
	ui_WindowAddChild(window, cancelButton);
	ui_WindowSetModal(window, true);
	window->widget.priority = selectWin->widget.priority + 1;
	//ui_WindowSetCloseCallback(window, newFolderCloseWindow, NULL);
	ui_WindowSetClosable(window, false); // So that Escape doesn't close both modal windows
	ui_WindowPresent(window);
	ui_SetFocus(newFolderNameEntry);

	newFolderWin = window;
}

// cancel button callback
static void cancelClick(UIButton *cancelButton, UserData stuff)
{
	if (exitFunc)
		exitFunc(exitData);	
	ui_FileBrowserFree();
}

// close browser callback
static bool closeFileBrowser(UIWindow *window, UserData cancelData)
{
	if (overwriteWin || newFolderWin)
		return false; // Do not close, we're not in the front
	if (exitFunc)
		exitFunc(cancelData);
	ui_FileBrowserFree();
	return true;
}

static void nameSpaceSelectedCB(void *unused, void *unused2)
{
	int it;
	char nameSpacePrefix[MAX_PATH];
	const char *newNameSpaceName = ui_ComboBoxGetSelectedObject(nameSpaces);
	bool useNameSpace = (newNameSpaceName && stricmp(newNameSpaceName, NO_NAMESPACE_STR));

	if(eaSize(&topmostDirs) != eaSize(&origTopmostDirs))
		return;

	if(useNameSpace)
		sprintf(nameSpacePrefix, NAMESPACE_PATH"%s/", newNameSpaceName);

	for( it = 0; it != eaSize(&topmostDirs); ++it ) {
		free( topmostDirs[ it ]);
		topmostDirs[ it ] = malloc( MAX_PATH );

		if(useNameSpace) {
			strcpy_s(topmostDirs[ it ], MAX_PATH, nameSpacePrefix);
			strcat_s(topmostDirs[ it ], MAX_PATH, origTopmostDirs[ it ]);
		} else {
			strcpy_s(topmostDirs[ it ], MAX_PATH, origTopmostDirs[ it ]);
		}
	}

	if(topmostDirs)
		loadDir( topmostDirs[ 0 ], "", true );
}

// text entry Enter button callback
static void folderNameEnter(UIWidget *widget, UserData stuff)
{
	loadDir(ui_TextEntryGetText(activeFolder), "", true);
}

static void folderNameFinish(UIWidget *widget, UserData stuff)
{
	ui_TextEntrySetText(activeFolder, activeFolderPath);
}

static void fileListSelect(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	ui_ListSetSelectedRowCol(pList, iRow, iColumn);
	ui_TextEntrySetText(nameEntry, (char*)ui_ListGetSelectedObject(folderContents));
}

UIWindow *ui_FileBrowserCreate(
        const char *title, const char *buttonText, UIBrowserMode mode,
        UIBrowserType type, bool excludePrivateDirs, const char *topDir,
		const char *startDir, const char *startText,
        const char *defaultExt, UICancelFunc cancelF, UserData cancelData,
        UIFileSelectFunc selectF, UserData newSelectData)
{
    static const char** topDirs = NULL;
    static const char** defaultExts = NULL;

    if( !defaultExts ) {
        eaCreate( &defaultExts );
    }
    eaSetSize( &defaultExts, 0 );
    eaPush( &defaultExts, defaultExt );

    if( !topDirs ) {
        eaCreate( &topDirs );
    }
    eaSetSize( &topDirs, 0 );
    eaPush( &topDirs, topDir );

    return ui_FileBrowserCreateEx(
            title, buttonText, mode, type, excludePrivateDirs, topDirs,
			startDir, startText, defaultExts, cancelF, cancelData, selectF,
			newSelectData, NULL, NULL, NULL, false );
}

void ui_FileBrowserSetSelectedList(const char** eaSelections)
{
	if (browseMode == UIBrowseMultipleExisting)
	{
		int i;
		for (i = 0; i < eaSize(&eaSelections); i++)
		{
			if (eaFindCmp(&selectedFileNames, eaSelections[i], inverseStrCmp) == -1)
			{
				eaPush(&selectedFileNames, strdup(eaSelections[i]));
				eaPush(&selectedFilePaths, NULL);
				ui_ListSetModel(selectedFiles, NULL, &selectedFileNames);
			}
		}
	}
}

UIWindow *ui_FileBrowserCreateEx(
        const char *title, const char *buttonText, UIBrowserMode mode,
        UIBrowserType type, bool excludePrivateDirs, const char **topDirs,
		const char *startDir, const char *startText,
        const char **defaultExts, UICancelFunc cancelF, UserData cancelData,
        UIFileSelectFunc selectF, UserData newSelectData,
        UIFilterFunc filterF, UserData filterData, UIMultiFileSelectFunc multiF, bool showNameSpacesUI)
{
	UIButton *cancel, *ok, *upFolder, *newFolder;
	UIListColumn *fileNameCol;
	UILabel *namePrompt;
	UILabel *nameSpaceLable;
	int x, y, i;
	char startDirLocal[MAX_PATH];
	void ***prevDirs = calloc(1, sizeof(void **));
	char valueName[128];
	int width = 400;
	int height = 250;

	startDirLocal[0] = 0;

	// set the global variables to keep track of the parameters
	// (set first because they're used in some of the callbacks from functions called below)
	browseMode = mode;
	browseType = type;
	selectFunc = selectF;
	selectData = newSelectData;
	multiSelectFunc = multiF;
	exitFunc = cancelF;
	exitData = cancelData;
	nameFilterFunc = filterF;
	nameFilterData = filterData;
	noPrivateDirs = excludePrivateDirs;
	showNameSpaces = showNameSpacesUI;

	if (startDir)
		fileRelativePath(startDir, startDirLocal);

	eaClearEx(&origTopmostDirs, StructFreeString);
    {
        int topmostSize = eaSize( &topmostDirs );
        int size = eaSize( &topDirs );
        int it;

        for( it = 0; it != topmostSize; ++it ) {
            free( topmostDirs[ it ]);
            topmostDirs[ it ] = NULL;
        }
        eaSetSize( &topmostDirs, size );
        for( it = 0; it != size; ++it ) {
            topmostDirs[ it ] = malloc( MAX_PATH );
            fileRelativePath_s( topDirs[ it ], topmostDirs[ it ], MAX_PATH );
            forwardSlashes(topmostDirs[ it ]);
			eaPush(&origTopmostDirs, StructAllocString(topmostDirs[ it ]));
        }
    }
	forwardSlashes(startDirLocal);

	// validate the directory inputs; make sure startDir is a subdirectory of topDir
	if (!startDirLocal[0] || !strStartsWithAny(startDirLocal, topmostDirs) || !dirExists(startDirLocal) || (noPrivateDirs && strstr(startDirLocal, "/_")))
		strcpy(startDirLocal, topmostDirs[ 0 ]);

	if ( !eaSize( &defaultExts ) || !defaultExts[ 0 ] || !defaultExts[ 0 ][ 0 ])
		ext[0] = 0;
	else if ( defaultExts[ 0 ][ 0 ] == '.')
		strcpy(ext, defaultExts[ 0 ]);
	else
		sprintf(ext, ".%s", defaultExts[ 0 ]);

	eaDestroyEx(&validExts, NULL);
	for (i = 0; i < eaSize(&defaultExts); i++)
		eaPush(&validExts, strdup(defaultExts[i]));

	// set up widgets1
	getRememberedWidthHeight(&width, &height);
	selectWin = ui_WindowCreate(title, 100, 100, width, height);

	// set window position
	rdrGetDeviceSize(gfxGetActiveOrPrimaryDevice(), NULL, NULL, &x, &y, NULL, NULL, NULL, NULL);
	x -= selectWin->widget.width;
	x /= 2;
	y -= selectWin->widget.height;
	y /= 2;
	selectWin->widget.x = x;
	selectWin->widget.y = y;

	// read the previous folders from the registry
	for (i = 0; i < BROWSER_NUM_PREV; i++)
	{
		const char *prevFolderName;
		sprintf(valueName, BROWSER_REG_FORMAT, i + 1);
		if (prevFolderName = GamePrefGetString(valueName, NULL))
			eaPush(prevDirs, strdup(prevFolderName));
		else
			break;
	}
	prevFolders = ui_ComboBoxCreate(40, 10, 200, NULL, prevDirs, NULL);

	activeFolder = ui_TextEntryCreate(startDirLocal, 0, 10);
	activeFolder->widget.width = 1;
	activeFolder->widget.widthUnit = UIUnitPercentage;
	activeFolder->widget.leftPad = 90;
	activeFolder->widget.rightPad = 10;
	ui_TextEntrySetComboBox(activeFolder, prevFolders);
	ui_TextEntrySetEnterCallback(activeFolder, folderNameEnter, NULL);
	ui_TextEntrySetFinishedCallback(activeFolder, folderNameFinish, NULL);
	strcpy(activeFolderPath, startDirLocal);

	upFolder = ui_ButtonCreate("UP", 10, 10, upClick, NULL);
	if (mode != UIBrowseExisting)
		newFolder = ui_ButtonCreate("New...", 38, 10, newClick, NULL);

	fileScanAllDataDirs(startDirLocal, compileFileList, NULL);
	if (fileNames)
		eaQSort(fileNames, compareFileNames);
    
	if(showNameSpaces) {
		const char **fileNameSpaces = fileGetNameSpaceNameList();
		eaClear(&nameSpaceList);
		eaCopy(&nameSpaceList, &fileNameSpaces);
		eaInsert(&nameSpaceList, NO_NAMESPACE_STR, 0);

		nameSpaceLable = ui_LabelCreate("Name Space: ", 10, 40);
		ui_WindowAddChild(selectWin, nameSpaceLable);
		nameSpaces = ui_ComboBoxCreate(0, 40, 200, NULL, &nameSpaceList, NULL);
		ui_ComboBoxSetSelected(nameSpaces, 0);
		ui_ComboBoxSetSelectedCallback(nameSpaces, nameSpaceSelectedCB, NULL);
		nameSpaces->widget.width = 1;
		nameSpaces->widget.widthUnit = UIUnitPercentage;
		nameSpaces->widget.leftPad = 90;
		nameSpaces->widget.rightPad = 10;
		ui_WindowAddChild(selectWin, nameSpaces);
	}
	
	folderContents = ui_ListCreate(NULL, &fileNames, 15);
	folderContents->widget.x = 0;
	folderContents->widget.y = 0;
	folderContents->widget.width = (mode == UIBrowseMultipleExisting) ? 0.5 : 1;
	folderContents->widget.height = 1;
	folderContents->widget.leftPad = folderContents->widget.rightPad = 10;
	folderContents->widget.topPad = (showNameSpaces ? 70 : 40);
	folderContents->widget.bottomPad = 70;
	folderContents->widget.widthUnit = folderContents->widget.heightUnit = UIUnitPercentage;
	ui_ListSetCellClickedCallback(folderContents, fileListSelect, NULL);
	ui_ListSetActivatedCallback(folderContents, fileListActivate, selectData);
	fileNameCol = ui_ListColumnCreateCallback("File Name", displayFileName, NULL);
	fileNameCol->fWidth = 300;
	ui_ListAppendColumn(folderContents, fileNameCol);
    if( type == UIBrowseTextureFiles ) {
        UIListColumn* previewColumn;

        folderContents->fRowHeight = 64;
        
        previewColumn = ui_ListColumnCreateTexture( "Preview", displayTexturePreview, NULL );
        previewColumn->fWidth = 64;
        previewColumn->bResizable = false;
        previewColumn->bAutoSize = false;
        ui_ListAppendColumn(folderContents, previewColumn);
    }

	if (mode == UIBrowseMultipleExisting)
	{
		selectedFiles = ui_ListCreate(NULL, &selectedFileNames, 15);
		selectedFiles->widget.x = 0;
		selectedFiles->widget.y = 0;
		selectedFiles->widget.width = 0.5;
		selectedFiles->widget.height = 1;
		selectedFiles->widget.leftPad = selectedFiles->widget.rightPad = 10;
		selectedFiles->widget.topPad = (showNameSpaces ? 70 : 40);
		selectedFiles->widget.bottomPad = 70;
		selectedFiles->widget.widthUnit = selectedFiles->widget.heightUnit = UIUnitPercentage;
		selectedFiles->widget.offsetFrom = UITopRight;
		ui_ListSetActivatedCallback(selectedFiles, removeFileFromSelectionList, selectData);
		fileNameCol = ui_ListColumnCreateCallback("Selected Files", displayFileName, NULL);
		fileNameCol->fWidth = 300;
		ui_ListAppendColumn(selectedFiles, fileNameCol);
		if( type == UIBrowseTextureFiles ) {
			UIListColumn* previewColumn;

			selectedFiles->fRowHeight = 64;

			previewColumn = ui_ListColumnCreateTexture( "Preview", displayTexturePreview, NULL );
			previewColumn->fWidth = 64;
			previewColumn->bResizable = false;
			previewColumn->bAutoSize = false;
			ui_ListAppendColumn(selectedFiles, previewColumn);
		}
	}

	namePrompt = ui_LabelCreate("Name: ", 10, 40);
	namePrompt->widget.offsetFrom = UIBottomLeft;
	nameEntry = ui_TextEntryCreate(startText ? startText : "", 10, 40);
	nameEntry->widget.width = (mode == UIBrowseMultipleExisting) ? 0.5 : 1;
	nameEntry->widget.leftPad = 50;
	nameEntry->widget.rightPad = 10;
	nameEntry->widget.offsetFrom = UIBottomLeft;
	nameEntry->widget.widthUnit = UIUnitPercentage;
	ui_TextEntrySetEnterCallback(nameEntry, okClick, NULL);
    ui_TextEntrySetChangedCallback(nameEntry, filterChanged, NULL);
    
	cancel = ui_ButtonCreate("Cancel", 10, 10, cancelClick, cancelData);
	cancel->widget.offsetFrom = UIBottomRight;
	ok = ui_ButtonCreate(buttonText, 60, 10, okClick, selectData);
	ok->widget.offsetFrom = UIBottomRight;

	//ui_WindowAddChild(selectWin, prevFolders);
	ui_WindowAddChild(selectWin, activeFolder);
	ui_WindowAddChild(selectWin, upFolder);
	if (mode != UIBrowseExisting)
		ui_WindowAddChild(selectWin, newFolder);
	ui_WindowAddChild(selectWin, folderContents);
	if (mode == UIBrowseMultipleExisting)
		ui_WindowAddChild(selectWin, selectedFiles);
	ui_WindowAddChild(selectWin, namePrompt);
	ui_WindowAddChild(selectWin, nameEntry);
	ui_WindowAddChild(selectWin, cancel);
	ui_WindowAddChild(selectWin, ok);
	ui_WindowSetCloseCallback(selectWin, closeFileBrowser, cancelData);

	// file browse dialog will be modal
    ui_SetFocus(nameEntry);
	ui_WindowSetModal(selectWin, true);

	return selectWin;
}
