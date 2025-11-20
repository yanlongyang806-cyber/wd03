#include "patchtrivia.h"
#include "trivia.h"
#include "pcl_typedefs.h"
#include "sysutil.h"	// getExecutableName()
#include "fileutil.h"	// fileGetcwd
#include "error.h"		// ErrorFilenamef
#include "utilitiesLib.h"
#include "utils.h"
#include "winutil.h"

static TriviaList* triviaListGetPatchTriviaForFileCached(const char *file, char *rootdir, int rootdir_size);

// We cache the trivia list so the repeated queries won't have to reload the file.
// Calling triviaListWritePatchTriviaToFile() invalidates this cache.
// Note that this is not invalidated by writes in other processes.
static TriviaList *s_cached_trivia_list = NULL;

// The root matching the above cache.
static char s_cached_trivia_root[MAX_PATH];

bool getPatchTriviaList(char *triviapath, int triviapath_size, char *rootpath, int rootpath_size, const char *file)
{
	char *slash;

	if(!rootpath)
	{
		rootpath_size = MAX_PATH;
		rootpath = _alloca(rootpath_size);
	}

	if(file)
	{
		if (!*file)
			return false;
		strcpy_s(rootpath, rootpath_size, file);
		forwardSlashes(rootpath);
		if(strEndsWith(rootpath, "/")){
			rootpath[strlen(rootpath) - 1] = 0;
		}
	}
	else
	{
		strcpy_s(rootpath, rootpath_size, getExecutableName());
		forwardSlashes(rootpath);
		slash = strrchr(rootpath,'/');
		if(slash)
			*slash = '\0';
	}

	if(is_pigged_path(rootpath)) // invalid path, will cause assert in file layer (thinks it's a pigged path)
		return false;

	for(;;)
	{
		sprintf_s(SAFESTR2(triviapath), "%s/%s/%s", rootpath, PATCH_DIR, TRIVIAFILE_PATCH); // Secret knowledge
		if(fileExists(triviapath))
			return true;
		else 
		{
			strcat_s(SAFESTR2(triviapath), TRIVIAFILE_PATCH_OLD);
			if(fileExists(triviapath))
				return true;
			else {
				sprintf_s(SAFESTR2(triviapath), "%s/%s/", rootpath, PATCH_DIR);
				if(dirExists(triviapath)) {
					strcat_s(SAFESTR2(triviapath), TRIVIAFILE_PATCH);
					return true;
				}
			}
		}

		slash = strrchr(rootpath, '/');
		if(!slash)
		{
			rootpath[0] = '\0';
			return false;
		}
		*slash = '\0';
	}
}

void triviaPrintPatchTrivia(void)
{
	char trivia_file[MAX_PATH];
	if(getPatchTriviaList(SAFESTR(trivia_file), NULL, 0, NULL))
	{
		TriviaMutex mutex = triviaAcquireDumbMutex(trivia_file);
		triviaPrintFromFile("", trivia_file);
		triviaReleaseDumbMutex(mutex);
	}
}

bool triviaGetPatchTriviaForFile(char *buf, int buf_size, const char *file, const char *key)
{
	bool bFound = false;
	TriviaList *list;

	if(buf_size > 0)
		buf[0] = '\0';

	list = triviaListGetPatchTriviaForFileCached(file, NULL, 0);
	if(list)
	{
		const char *val = triviaListGetValue(list, key);
		if(val)
		{
			strcpy_s(buf, buf_size, val);
			bFound = true;
		}
	}

	return bFound;
}

bool triviaCheckPatchCompletion(const char *file, const char *patchname, int branch, U32 time, const char *sandbox, bool requiredOnly)
{
	TriviaList *list;
	bool bComplete = false;

	list = triviaListGetPatchTriviaForFileCached(file, NULL, 0);
	if(list)
	{
		const char *val;

		if( (val = triviaListGetValue(list, "PatchComplete")) &&
			(stricmp(val,"All")==0 || stricmp(val,"Required")==0 && requiredOnly) )
		{
			if(patchname && (val = triviaListGetValue(list,"PatchName")) && stricmp(patchname, val)==0)
			{
				bComplete = true;
			}
			else
			{
				if (sandbox)
				{
					ANALYSIS_ASSUME(sandbox);
					val = triviaListGetValue(list, "PatchBranch");
					if (val)
					{
						ANALYSIS_ASSUME(val);
						if (atoi(val) == branch)
						{
							val = triviaListGetValue(list, "PatchTime");
							if (val)
							{
								ANALYSIS_ASSUME(val);
								if (atoi(val) == (int)time)
								{
									val = triviaListGetValue(list, "PatchSandbox");
									if (val)
									{
										ANALYSIS_ASSUME(val);
										if (stricmp(sandbox, val)==0)
										{
											bComplete = true;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return bComplete;
}

void triviaSetPatchCompletion(const char *file, bool requiredOnly)
{
	char trivia_file[MAX_PATH];

	if(getPatchTriviaList(SAFESTR(trivia_file), NULL, 0, file))
	{
		TriviaMutex mutex = triviaAcquireDumbMutex(trivia_file);
		TriviaList *list = triviaListCreateFromFile(trivia_file);
		if(list)
		{
			
			triviaListPrintf(list, "PatchComplete", "%s", requiredOnly ? "Required" : "All");
			// Check to make sure we're not writing a trivia file with a core mapping into a non-core folder
			if(triviaListGetValue(list, "PatchProject") && strstri(trivia_file, "Core") && !strstri(triviaListGetValue(list, "PatchProject"), "Core"))
				Errorf("Writing non-core project (%s) data into a folder named Core (%s)", triviaListGetValue(list, "PatchProject"), trivia_file);
			triviaListWritePatchTriviaToFile(list, trivia_file);
			triviaReleaseDumbMutex(mutex);
			triviaListDestroy(&list);
		}
		else
		{
			ErrorFilenamef(trivia_file, "Could not open patch trivia %s", trivia_file);
		}
	}
}

// Return true if a file's path indicates that it is in a directory, including if the file is the directory itself.
static bool fileIsInDirectory(const char *file, const char *dir)
{
	if (!file || !*file || !dir || !*dir)
		return false;

	return !stricmp(file, dir) || strStartsWith(file, dir) && strlen(file) > strlen(dir) && file[strlen(dir)] == '/';
}

// This is for internal use, where the caller will not free it, so we can cache the value.
static TriviaList* triviaListGetPatchTriviaForFileCached(const char *file, char *rootdir, int rootdir_size)
{
	char rootdir_buffer[MAX_PATH];
	char trivia_file[MAX_PATH];
	bool success;
	TriviaMutex mutex;
	TriviaList *list;

	// Invalidate the cache if the root has changed.
	if (s_cached_trivia_list && !fileIsInDirectory(file, s_cached_trivia_root))
		triviaListDestroy(&s_cached_trivia_list);

	// If there's a cached trivia list, use that.
	if (s_cached_trivia_list)
	{
		if (rootdir)
			strcpy_s(SAFESTR2(rootdir), s_cached_trivia_root);
		return s_cached_trivia_list;
	}

	// Try to find trivia.
	if (!rootdir)
	{
		rootdir = rootdir_buffer;
		rootdir_size = sizeof(rootdir_buffer);
	}
	success = getPatchTriviaList(SAFESTR(trivia_file), rootdir, rootdir_size, file);
	if (!success)
		return NULL;

	// Acquire mutex.
	mutex = triviaAcquireDumbMutex(trivia_file);

	// Open trivia file.
	list = triviaListCreateFromFile(trivia_file);
	strcat(trivia_file, TRIVIAFILE_PATCH_OLD);
	if (list)
	{
		// If we opened the trivia successfully, delete the old trivia, if any.
		if (fileExists(trivia_file))
			fileForceRemove(trivia_file);	// It's not a huge deal if this fails.  Since we're not protecting against concurrent access, it will
											// happen occasionally when two separate processes are reading from a patch_trivia.txt at once.
	}
	else
	{
		// Try to open the old trivia.
		if (fileExists(trivia_file))
		{
			ErrorFilenamef(trivia_file, "Couldn't open primarily patch trivia file, trying fallback");
			list = triviaListCreateFromFile(trivia_file);

		}
		else
			ErrorFilenamef(trivia_file, "Unable to open patch trivia file");

		if (!list)
			Errorf("No patch trivia file could be loaded");
	}

	// Release mutex.
	triviaReleaseDumbMutex(mutex);

	// Cache the trivia.
	s_cached_trivia_list = list;
	strcpy(s_cached_trivia_root, rootdir);

	return list;
}

// Find and open the patch trivia for a specific file.
// This returns our cached copy, so that the caller can free it.
TriviaList* triviaListGetPatchTriviaForFile(const char *file, char *rootdir, int rootdir_size)
{
	TriviaList *list = triviaListGetPatchTriviaForFileCached(file, SAFESTR2(rootdir));

	// Release the cache, since we're transferring ownership to the caller.
	s_cached_trivia_list = NULL;

	return list;
}

extern errno_t errno;
bool triviaListWritePatchTriviaToFile(SA_PARAM_NN_VALID TriviaList *list, SA_PARAM_OP_STR const char *file)
{
	char trivia_path[MAX_PATH];
	char root_path[MAX_PATH];

	// Destroy any cache.
	if (s_cached_trivia_list)
		triviaListDestroy(&s_cached_trivia_list);

	if(getPatchTriviaList(SAFESTR(trivia_path), SAFESTR(root_path), file))
	{
		char new_path[MAX_PATH];
		bool was_bak = false;
		
		// Check if we were looking at a backup
		if(strEndsWith(trivia_path, TRIVIAFILE_PATCH_OLD))
		{
			was_bak = true;
			trivia_path[strlen(trivia_path)-4] = 0;
		}

		// Write new trivia to patch_trivia.txt.new
		sprintf(new_path, "%s/%s/%s%s", root_path, PATCH_DIR, TRIVIAFILE_PATCH, TRIVIAFILE_PATCH_NEW);
		triviaListWriteToFile(list, new_path);

		// NOTE: If there was no old patch_trivia.txt, we are done.
		if(!fileExists(trivia_path))
		{
			backSlashes(new_path);
			backSlashes(trivia_path);
			if(!rename(new_path, trivia_path))
				return true;
			else
				return false;
		}

		// Move patch_trivia.txt to patch_trivia.txt.bak
		if(was_bak || !fileRenameToBak(trivia_path))
		{
			// Move patch_trivia.txt.new to patch_trivia.txt
			if(!rename(new_path, trivia_path))
			{
				// The old trivia file will be deleted the first time that triviaOpenPatchTriviaForFile() reads the new trivia file successfully.
				return true;
			}
		}
	}
	return false;
}


const char *amIASimplePatchedApp(void)
{
	char exeDir[CRYPTIC_MAX_PATH];
	char tempDir[CRYPTIC_MAX_PATH];

	getExecutableDir(exeDir);
	sprintf(tempDir, "%s/.patch", exeDir);
	if (!dirExists(tempDir))
	{
		return NULL;
	}

	sprintf(tempDir, "%s/piggs", exeDir);
	if (!dirExists(tempDir))
	{
		return NULL;
	}

	return GetUsefulVersionString();
}

void updateAndRestartSimplePatchedApp(void)
{
	FILE *pBatFile;
	char exeDir[CRYPTIC_MAX_PATH];
	char project[256];
	char server[256];
	char executable[CRYPTIC_MAX_PATH];

	getExecutableDir(exeDir);
	backSlashes(exeDir);

	if (!triviaGetPatchTriviaForFile(SAFESTR(project), exeDir, "PatchProject"))
	{
		return;
	}
	if (!triviaGetPatchTriviaForFile(SAFESTR(server), exeDir, "PatchServer"))
	{
		return;
	}
	if (!triviaGetPatchTriviaForFile(SAFESTR(executable), exeDir, "PatchExecutable"))
	{
		return;
	}

	
	pBatFile = fopen("_restart.bat", "wt");
	if (!pBatFile)
	{
		return;
	}
	fprintf(pBatFile, "sleep 10\n");
	fprintf(pBatFile, "cd %s\\..\n", exeDir);
	fprintf(pBatFile, "%s -sync -project %s -server %s\n", executable, project, server);
	fprintf(pBatFile, "cd %s\n", exeDir);
	fprintf(pBatFile, "%s\n", GetCommandLineCryptic());

	fclose(pBatFile);

	system_detach("cmd.exe /c _restart.bat", false, false);

	exit(0);
}



