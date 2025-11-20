#pragma once

// Patch Trivia, the following keys may be defined
// PatchProject
// PatchServer
// PatchPort
// PatchName					- if view is named
// PatchBranch
// PatchTime
// PatchTimeReadable
// PatchSandbox
// PatchIncrementalFrom			- if view is an incremental
// PatchIncrementalFromReadable	- if view is an incremental
// PatchCmdLine	- simulated command line for stand-alone patchclient
// CoreBranchMapping

// Warning: The problem with this file is that the conceptual interface here is not very well defined, and the transition to
// Noah's new patch trivia was incomplete.  Some code calls into this function to get trivia data, which seems to be the intention,
// but even more code just uses this to get the trivia and then calls trivia.h functions to get data, and then some code bypasses
// most or all of this and uses TRIVIAFILE_PATCH or even "patch_trivia.txt" directly for its own devices.  I have a feeling that this
// code was not completely thought out, and checked in incomplete.  Nonetheless, I think the convention we should use going forward
// is to use triviaGetPatchTriviaForFile() to get trivia values, and only use triviaListGetPatchTriviaForFile() when we want to later
// call triviaListWritePatchTriviaToFile.  Regardless, we should not add any new code outside of patchtrivia.c that refers to TRIVIAFILE_PATCH
// or "patch_trivia.txt" directly.
// The reason all of the above is relevant is that these functions enforce various guarantees and semantics, such as the proper way to find
// the .patch folder, and patch_trivia.txt writing transaction integrity.

#define TRIVIAFILE_PATCH	"patch_trivia.txt"
#define TRIVIAFILE_PATCH_OLD	".bak"
#define TRIVIAFILE_PATCH_NEW	".new"

typedef struct TriviaList TriviaList;

// Find the trivia file and root path for a given file in a PCL working copy.
bool getPatchTriviaList(char *triviapath, int triviapath_size, char *rootpath, int rootpath_size, const char *file);

// adds any available patch trivia for the current executable
void triviaPrintPatchTrivia(void);

//returns true if it found something, false otherwise
// searches up for a .patch directory, starting at 'file' or the current executable if file is NULL
bool triviaGetPatchTriviaForFile(char *buf, int buf_size, const char *file, const char *key);

// returns true if the patch has already been applied successfully
//  searches up for a .patch directory, starting at 'file' or the current executable if file is NULL
//  checks for patchname if given, otherwise branch/time/sandbox
//  if requiredOnly is set, checks that all required files had been successfully patched, otherwise check that all files had been successfully patched
bool triviaCheckPatchCompletion(const char *file, const char *patchname, int branch, U32 time, const char *sandbox, bool requiredOnly);

// Set the PatchCompletion value for the patch trivia.
void triviaSetPatchCompletion(const char *file, bool requiredOnly);

// Find and open the patch trivia for a specific file, and acquire the mutex.
TriviaList* triviaListGetPatchTriviaForFile(const char *file, char *rootdir, int rootdir_size); // root directory returned in rootdir

// Write out the given TriviaList to the patch_trivia file under `file`. If `file` is NULL, use the current executable.
bool triviaListWritePatchTriviaToFile(SA_PARAM_NN_VALID TriviaList *list, SA_PARAM_OP_STR const char *file);

//useful for simple cryptic apps like ShardLauncher that can be patched, or can be launched from c:\core\tools\bin. Returns the
//current patched version string if patched, NULL otherwise
const char *amIASimplePatchedApp(void);
void updateAndRestartSimplePatchedApp(void);
