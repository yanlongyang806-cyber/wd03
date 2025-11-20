/***************************************************************************



***************************************************************************/

#ifndef UI_FILEBROWSER_H
#define UI_FILEBROWSER_H

#include "UICore.h"
#include "UIWindow.h"

typedef enum
{
	UIBrowseExisting,
	UIBrowseNew,
	UIBrowseNewNoOverwrite,
	UIBrowseNewOrExisting,
	UIBrowseMultipleExisting
} UIBrowserMode;

typedef enum
{
	UIBrowseFiles,
	UIBrowseFolders,
    UIBrowseTextureFiles,
} UIBrowserType;

typedef void (*UIFileSelectFunc) (const char *, const char *, UserData);
typedef void (*UIMultiFileSelectFunc) (const char **, const char **, UserData);
typedef void (*UIFilterFunc) (char*, int, const char*, UserData);
typedef void (*UICancelFunc) (UserData);

/*
 * Creating a file browser returns a modal file browser window that lets you navigate a directory hierarchy
 * and select a file or folder.  When this selection is complete, the full path to the selected file/folder
 * is passed to the UIFileSelectFunc callback.  If cancel is pressed, a UICancelFunc callback is invoked
 * with no parameters instead.  The parameters to the file browser function are as follows:
 *   title - the title of the file browsing window
 *   buttonText - the text of the confirm button (eg. "Open", "Save")
 *   browseMode - UIBrowseExisting will ensure the user selects only existing files; UIBrowseNew allows
 *                the user to specify a new filename
 *   browseType - UIBrowseFiles forces the user to select (or enter) a file name, while UIBrowseFolders
 *                will just return the name of the active folder
 *   topDirs - the topmost directories the user is allowed to traverse to
 *   startDir - the directory where the user's navigation begins
 *   defaultExts - extension filter for files to display, where the first element is the default
 *                 extension of new files if no extension is specified by the user
 *   cancelF - the cancel button callback
 *   selectF - the confirm button callback
 *
 * NOTES:
 *   *Directory name format is currently quite restrictive: they must use backslashes instead of forward
 *    slashes and should not end in a backslash.  This functionality will be refined in the future to be
 *    more tolerant of name formats.
 *   *To get rid of the file browser window, you cannot just call ui_FileBrowserFree in your cancel/select
 *    callbacks, as the UI draw function will continue to try to render the freed widgets.  The callbacks
 *    should set a flag that is detected outside of the UI draw function to free the browser window.
 */

void ui_FileBrowserFree(void);
void ui_FileBrowserSetSelectedList(const char** eaSelections);

SA_RET_NN_VALID UIWindow *ui_FileBrowserCreate(SA_PARAM_NN_STR const char *title, SA_PARAM_NN_STR const char *buttonText,
										   UIBrowserMode browseMode, UIBrowserType browseType, bool excludePrivateDirs,
										   SA_PARAM_NN_STR const char *topDir, SA_PARAM_OP_STR const char *startDir,
										   SA_PARAM_OP_STR const char *startText, SA_PARAM_OP_STR const char *defaultExt,
										   UICancelFunc cancelF, UserData cancelData, UIFileSelectFunc selectF,
										   UserData selectData);
SA_RET_NN_VALID UIWindow *ui_FileBrowserCreateEx(SA_PARAM_NN_STR const char *title, SA_PARAM_NN_STR const char *buttonText,
                                             UIBrowserMode browseMode, UIBrowserType browseType, bool excludePrivateDirs,
                                             SA_PARAM_NN_STR const char **topDirs, SA_PARAM_OP_STR const char *startDir,
                                             SA_PARAM_OP_STR const char *startText, SA_PARAM_OP_STR const char **defaultExts,
										     UICancelFunc cancelF, UserData cancelData,
                                             UIFileSelectFunc selectF, UserData selectData,
                                             UIFilterFunc filterF, UserData filterData, UIMultiFileSelectFunc multiF, bool showNameSpacesUI);

#endif // UI_FILEBROWSER_H
