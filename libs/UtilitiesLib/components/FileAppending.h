#pragma once

//this file contains code for taking a bunch of binary files and appending them to the end of another file, then
//later on extracting them. This is used for installing.

//appends all files in pTargetDirectory onto the end of pSourceFile, writes output into
//pTargetFileName. Returns number of files appended, negative on error. The files
//are stored with names relative to pTargetDirectory. pCommentString is also written
//into the file
int AppendFiles(char *pSourceFile, char *pTargetFileName, char *pTargetDirectory, char *pCommentString);

//checks if a file has appended files. If it does, get its comment string into estring ppCommentString
bool FileHasAppendedFiles(char *pFileName, char **ppCommentString);

void ExtractAppendedFiels(char *pFileName, char *pTargetDirectory);

