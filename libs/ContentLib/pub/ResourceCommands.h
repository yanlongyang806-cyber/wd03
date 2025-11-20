#pragma once
GCC_SYSTEM

#ifndef RESOURCECOMMANDS_H_
#define RESOURCECOMMANDS_H_

#include "objPath.h"
#include "ResourceInfo.h"

// Structures and functions for various command-line activities dealing with object information

// Returns true if we need to do object commands
bool resAnyCommandsPending(void);

// Executes all pending object commands, returns 0 if any fail
int resExecuteAllPendingCommands(void);

// Outputs dictionary description to a file
int resOutputDictionaryDescription(const char *dictionaryName, const char *fileName);

// Outputs info on a specific object
int resOutputObjectDescription(const char *dictionaryName, const char *resourceName, const char *fileName);

// Perform a data copy
int resPerformDataCopy(const char *fileName);


#endif