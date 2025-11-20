// Functions for running the command-line patchclient.
// This interface is used by parts of the shards to find the proper patchclient to run.  Lower-level
// tools or scripts should probably have more direct ways of finding which patchclient to use, such as
// using one they've built, the one in in the PATH, or the one in c:\cryptic\tools\bin.  Alternately,
// programs that run on the player's machine should be using PatchClientLib directly, rather than
// the command-line interface.

#ifndef CRYPTIC_PATCHCLIENT_H
#define CRYPTIC_PATCHCLIENT_H

// Return a pointer to the command line that should be used to run patchclient.
// This is the normal function you should use to find patchclient.  All of the others are for special circumstances or where
// you need to get a specific part of the command line.  If in doubt, use this function.
const char *patchclientCmdLine(bool bit64);

// Like patchclientCmdLine(), but pass in some places to look for patchclient.
const char *patchclientCmdLineEx(bool bit64, const char *executable_directory, const char *core_executable_directory);

// Return a pointer to the full path of patchclient.exe or patchclientx64.exe that goes with the current build.
const char *patchclientFullPath(bool bit64);

// Like patchclientFullPath(), but pass in some places to look for patchclient.
const char *patchclientFullPathEx(bool bit64, const char *executable_directory, const char *core_executable_directory);

// Return a pointer to the filename that patchclient is known by.
const char *patchclientFilename(bool bit64);

// Return the "-SetPatchClient dir/patchclient.exe" option used to set the patchclient for a child process, calling patchclientFullPath().
const char *patchclientParameter(bool bit64);

// Return the "-SetPatchClient dir/patchclient.exe" option used to set the patchclient for a child process, calling patchclientFullPathEx().
const char *patchclientParameterEx(bool bit64, const char *executable_directory, const char *core_executable_directory);

#endif  // CRYPTIC_PATCHCLIENT_H
