/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "file.h"
#include "gimmeDLLWrapper.h"
#include "wininclude.h"
#include "utilitiesLib.h"
#include "autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define FOR_EACH_FILE(file, filename, x)							\
	if (file) {														\
		const char *filename;										\
		filename=file->filename;									\
		x;															\
	}
/*		for (i=eaSize(&file->linked_filenames)-1; i>=0; i--) {		\
			filename = file->linked_filenames[i];					\
			if (filename) {											\
				x;													\
			}														\
		}															\*/




// Handle the different messages gimme can give, returns 1 if no error
bool emuHandleGimmeMessage(const char *filename, GimmeErrorValue gimmeMessage, bool doing_checkout, bool show_dialog)
{
	char fullfilename[MAX_PATH];

	fileLocateWrite(filename, fullfilename);
	forwardSlashes(fullfilename);

	switch (gimmeMessage)
	{
		xcase GIMME_NO_ERROR:
		{
			return true;
		}
		xcase GIMME_ERROR_NOTLOCKEDBYYOU:
		{
			emStatusPrintf("\"%s\" not currently checked out.", fullfilename);
			if (show_dialog)
			{
				char buf[1024];
				sprintf(buf, "\"%s\" not currently checked out.", fullfilename);
				ui_DialogPopup("Error", buf);
			}
		}
		xcase GIMME_ERROR_ALREADY_CHECKEDOUT:
		{
			const char* lastAuthor = gimmeDLLQueryIsFileLocked(fullfilename);
			emStatusPrintf("\"%s\" already checked out by %s.", fullfilename, lastAuthor ? lastAuthor : "UNKNOWN");
			if (show_dialog)
			{
				char buf[1024];
				sprintf(buf, "\"%s\" already checked out by %s.", fullfilename, lastAuthor ? lastAuthor : "UNKNOWN");
				ui_DialogPopup("Error", buf);
			}
		}
		xcase GIMME_ERROR_NO_SC:
		case GIMME_ERROR_NO_DLL:
		{
			emStatusPrintf("You are not currently connected to source control.");
			if (show_dialog)
				ui_DialogPopup("Error", "You are not currently connected to source control.");
			if (doing_checkout)
				return true;
		}
		xcase GIMME_ERROR_FILENOTFOUND:
		{
			emStatusPrintf("File \"%s\" does not exist.", fullfilename);
			if (show_dialog)
			{
				char buf[1024];
				sprintf(buf, "File \"%s\" does not exist.", fullfilename);
				ui_DialogPopup("Error", buf);
			}
			if (doing_checkout)
				return true;
		}
		xcase GIMME_ERROR_NOT_IN_DB:
		{
			emStatusPrintf("File \"%s\" not in the Gimme database.", fullfilename);
			if (show_dialog)
			{
				char buf[1024];
				sprintf(buf, "File \"%s\" not in the Gimme database.", fullfilename);
				ui_DialogPopup("Error", buf);
			}
			if (doing_checkout)
				return true;
		}
	}
	return false;
}

int emuGetBranchNumber(void)
{
	static int branchNumber = -10;
	if (branchNumber < 0)
		branchNumber = fileGetGimmeDataBranchNumber();
	return branchNumber;
}

int emuOpenFile(const char *filename)
{
	char fullfilename[MAX_PATH];

	fileLocateWrite(filename, fullfilename);
	forwardSlashes(fullfilename);

	if (!fileExists(fullfilename))
	{
		emStatusPrintf("File \"%s\" does not exist.", fullfilename);
		return 0;
	}

	/*
	// fileOpenWithEditor() can no longer be a bool, because of the delayed shellexecute, which means no return value
	if (!fileOpenWithEditor(fullfilename))
	{
		emStatusPrintf("Failed to open the file \"%s\"", fullfilename);
		return 0;
	}
	*/
	fileOpenWithEditor(fullfilename);

	emStatusPrintf("File \"%s\" opened.", fullfilename);

	return 1;
}

void emuOpenContainingDirectory(const char *filename)
{
	char* directory;
	char fullfilename[MAX_PATH];

	fileLocateWrite(filename, fullfilename);
	forwardSlashes(fullfilename);

	directory = strrchr(fullfilename, '/');
	if (!directory)
		return;
	*directory = '\0';

	ulShellExecute(NULL, "open", fullfilename, NULL, NULL, SW_SHOW);
	emStatusPrintf("Opening the directory containing \"%s\"", fullfilename);
}

static void emuShowFileDoesNotExist(const char *title, const char *fullfilename, const char *name, bool show_dialog)
{
	if (name)
		emStatusPrintf("File \"%s\" for \"%s\" does not exist.", fullfilename, name);
	else
		emStatusPrintf("File \"%s\" does not exist.", fullfilename);

	if (show_dialog)
	{
		char buf[1024];
		if (name)
			sprintf(buf, "File \"%s\" for \"%s\" does not exist.", fullfilename, name);
		else
			sprintf(buf, "File \"%s\" does not exist.", fullfilename);

		ui_DialogPopup(title, buf);
	}
}

int emuCheckoutFileEx(EMFile *file, const char *name, bool show_dialog)
{
	char fullfilename[MAX_PATH];
	int ret=1;

	if (file->checked_out)
		return 1;

	FOR_EACH_FILE(file, filename, 
		fileLocateWrite(filename, fullfilename);
		forwardSlashes(fullfilename);

		if (fileExists(fullfilename)) {
			if (emuHandleGimmeMessage(fullfilename, gimmeDLLDoOperation(fullfilename, GIMME_CHECKOUT, 0), true, show_dialog))
			{
				if (name)
					emStatusPrintf("\"%s\" in file \"%s\" checked out.", name, fullfilename);
				else
					emStatusPrintf("File \"%s\" checked out.", fullfilename);
			} 
			else
				ret = 0;
		}
		else
			emuShowFileDoesNotExist("Check Out Failed", fullfilename, name, show_dialog);
	)

	return ret;
}

int emuCheckoutFile(EMFile *file)
{
	return emuCheckoutFileEx(file, NULL, false);
}

void emuCheckinFileEx(EMFile *file, const char *name, bool show_dialog)
{
	char fullfilename[MAX_PATH];

	FOR_EACH_FILE(file, filename, 
		fileLocateWrite(filename, fullfilename);
		forwardSlashes(fullfilename);

		if (fileExists(fullfilename))
		{
			char params[MAX_PATH+9];
			sprintf(params, "-put \"%s\"", fullfilename);
			ulShellExecute(NULL, "open", "gimme", params, NULL, SW_SHOW);
			if (name)
				emStatusPrintf("Checking in \"%s\" in file \"%s\"", name, fullfilename);
			else
				emStatusPrintf("Checking in file \"%s\"", fullfilename);

			//if (emuHandleGimmeMessage(fullfilename, gimmeCheckout(currentFile, GIMME_CHECKIN, 0), false, show_dialog))
			//	emuUpdateStatus(currentFile, "File checked in.");
		}
		else
			emuShowFileDoesNotExist("Check In Failed", fullfilename, name, show_dialog);
	)
}

void emuCheckinFile(EMFile *file)
{
	emuCheckinFileEx(file, NULL, false);
}

void emuCheckpointFileEx(EMFile *file, const char *name, bool show_dialog)
{
	char fullfilename[MAX_PATH];

	FOR_EACH_FILE(file, filename, 
		fileLocateWrite(filename, fullfilename);
		forwardSlashes(fullfilename);

		if (fileExists(fullfilename))
		{
			char params[MAX_PATH+9];
			sprintf(params, "-leavecheckedout -put \"%s\"", fullfilename);
			ulShellExecute(NULL, "open", "gimme", params, NULL, SW_SHOW);
			if (name)
				emStatusPrintf("Checkpointing \"%s\" in file \"%s\"", name, fullfilename);
			else
				emStatusPrintf("Checkpointing file \"%s\"", fullfilename);
		}
		else
			emuShowFileDoesNotExist("Checkpoint Failed", fullfilename, name, show_dialog);
	)
}

void emuCheckpointFile(EMFile *file)
{
	emuCheckpointFileEx(file, NULL, false);
}

void emuUndoCheckoutEx(EMFile *file, const char *name, bool show_dialog)
{
	char fullfilename[MAX_PATH];

	FOR_EACH_FILE(file, filename, 
		fileLocateWrite(filename, fullfilename);
		forwardSlashes(fullfilename);

		if (fileExists(fullfilename))
		{
			if (emuHandleGimmeMessage(fullfilename, gimmeDLLDoOperation(fullfilename, GIMME_UNDO_CHECKOUT, 0), false, show_dialog))
			{
				if (name)
					emStatusPrintf("\"%s\" in file \"%s\" reverted.", name, fullfilename);
				else
					emStatusPrintf("File \"%s\" reverted.", fullfilename);
			}
		}
		else
			emuShowFileDoesNotExist("Checkout Failed", fullfilename, name, show_dialog);
	)
}

void emuUndoCheckout(EMFile *file)
{
	emuUndoCheckoutEx(file, NULL, false);
}

void emuGetLatestEx(EMFile *file, const char *name, bool show_dialog)
{
	char fullfilename[MAX_PATH];

	FOR_EACH_FILE(file, filename, 
		fileLocateWrite(filename, fullfilename);
		forwardSlashes(fullfilename);

		if (fileExists(fullfilename))
		{
			if (emuHandleGimmeMessage(fullfilename, gimmeDLLDoOperation(fullfilename, GIMME_GLV, 0), false, show_dialog))
			{
				if (name)
					emStatusPrintf("Got the latest version of \"%s\" in \"%s\"", name, fullfilename);
				else
					emStatusPrintf("Got the latest version of \"%s\"", fullfilename);
			}
		}
		else
			emuShowFileDoesNotExist("Get Latest Failed", fullfilename, name, show_dialog);
	)
}

void emuGetLatest(EMFile *file)
{
	emuGetLatestEx(file, NULL, false);
}

void emuCheckRevisionsEx(EMFile *file, const char *name, bool show_dialog)
{
	char fullfilename[MAX_PATH];
	char *params = NULL;
	estrCreate(&params);

	FOR_EACH_FILE(file, filename, 
		fileLocateWrite(filename, fullfilename);
		forwardSlashes(fullfilename);

		if (fileExists(fullfilename))
		{
			estrPrintf(&params, "-stat \"%s\"", fullfilename);
			ulShellExecute(NULL, "open", "gimme", params, NULL, SW_SHOW);
			if (name)
			{
				emStatusPrintf("Getting file revision information for \"%s\" in file \"%s\"", name, fullfilename);
			}
			else
			{
				emStatusPrintf("Getting file revision information for \"%s\"", fullfilename);
			}
		}
		else
		{
			emuShowFileDoesNotExist("Gimme Stat Failed", fullfilename, name, show_dialog);
		}
	)
	estrDestroy(&params);
}

void emuCheckRevisions(EMFile *file)
{
	emuCheckRevisionsEx(file, NULL, false);
}

EMEditor gGenericFileEditor;

static EMEditorDoc *genericFileEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if(isDevelopmentMode())
		ServerCmd_gslOpenFile(pcName, pcType);

	return NULL;
}

#endif


AUTO_RUN;
int genericFileEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gGenericFileEditor.editor_name, "GenericFileEditor");
	gGenericFileEditor.type = EM_TYPE_MULTIDOC;
	gGenericFileEditor.disable_single_doc_menus = 1;
	gGenericFileEditor.disable_auto_checkout = 1;

	//gGenericFileEditor.init_func = genericFileEditorEMInit;
	gGenericFileEditor.load_func = genericFileEditorEMLoadDoc;

	emRegisterEditor(&gGenericFileEditor);
	emRegisterFileType("AIConfig", NULL, "GenericFileEditor");
#endif

	return 0;
}
