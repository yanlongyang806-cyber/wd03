#include "FileAppending.h"
#include "earray.h"
#include "fileutil2.h"
#include "utils.h"


/*Structure of a file with appended files:
----
Original file
----
strlen of appended file 1 name (U32)
----
name of appended file 1 (not null-terminated)
----
size of appended file 1 (U32)
----
appended file 1
----
strlen of appended file 2 name (U32)
----
name of appended file 2 (not null-terminated)
----
size of appended file 2 (U32)
----
appended file 2
----
...
----
comment string (not null-terminated, because its length is in the infoblock
----
AppendedFileInfoBlock
*/
#define FILEAPPENDING_MAGICNUMBER 0x1776149210661973

typedef struct
{
	U64 iMagicNumber;
	int iNumFiles;
	int iCommentStringLength;
	U32 iOriginalFileSize;
} AppendedFileInfoBlock;


#define BINARY_DUMP_BUFF_SIZE (16 * 1024 * 1024)
void BinaryDumpFileIntoFile(FILE *pInFile, FILE *pOutFile)
{
	char *pBuffer = malloc(BINARY_DUMP_BUFF_SIZE);
	size_t iBytesRead;

	do
	{
		iBytesRead = fread(pBuffer, 1, BINARY_DUMP_BUFF_SIZE, pInFile);

		if (iBytesRead)
		{
			fwrite(pBuffer, 1, iBytesRead, pOutFile);
		}
	}
	while (iBytesRead == BINARY_DUMP_BUFF_SIZE);

	free(pBuffer);
}

int AppendFiles(char *pSourceFile, char *pTargetFileName, char *pSourceDirectory, char *pCommentString)
{
	FILE *pOutFile;
	FILE *pInFile;
	char **ppFileList;
	char dupTargetFileName[CRYPTIC_MAX_PATH];
	char dupSourceDirectory[CRYPTIC_MAX_PATH];
	AppendedFileInfoBlock infoBlock = {0};
	int i;
	int iDirNameLength;

	strcpy(dupTargetFileName, pTargetFileName);
	strcpy(dupSourceDirectory, pSourceDirectory);

	forwardSlashes(dupSourceDirectory);

	while (strEndsWith(dupSourceDirectory, "/"))
	{
		dupSourceDirectory[strlen(dupSourceDirectory)-1] = 0;
	}

	iDirNameLength = (int)strlen(dupSourceDirectory);

	if (!pCommentString)
	{
		pCommentString = "";
	}


	pInFile = fopen(pSourceFile, "rb");
	if (!pInFile)
	{
		return -1;
	}

	infoBlock.iOriginalFileSize = fileSize(pSourceFile);
	infoBlock.iMagicNumber = FILEAPPENDING_MAGICNUMBER;
	infoBlock.iCommentStringLength = (int)strlen(pCommentString);


	mkdirtree(dupTargetFileName);
	pOutFile = fopen(dupTargetFileName, "wb");

	if (!pOutFile)
	{
		fclose(pInFile);
		return -1;
	}

	BinaryDumpFileIntoFile(pInFile, pOutFile);
	fclose(pInFile);

	ppFileList = fileScanDirFolders(dupSourceDirectory, FSF_FILES);

	infoBlock.iNumFiles = eaSize(&ppFileList);

	for (i=0; i < infoBlock.iNumFiles; i++)
	{
		size_t iTemp;

		pInFile = fopen(ppFileList[i], "rb");
		if (!pInFile)
		{
			fileScanDirFreeNames(ppFileList);
			fclose(pOutFile);
			return -1;
		}

		iTemp = strlen(ppFileList[i]) - iDirNameLength - 1;
		fwrite(&iTemp, 1, sizeof(size_t), pOutFile);
		fwrite(ppFileList[i] + iDirNameLength + 1, 1, iTemp, pOutFile);

		iTemp = fileSize(ppFileList[i]);
		fwrite(&iTemp, 1, sizeof(size_t), pOutFile);
		
		printf("About to append %s (%u bytes\n", ppFileList[i], (U32)iTemp);
		BinaryDumpFileIntoFile(pInFile, pOutFile);
		fclose(pInFile);

	}

	if (infoBlock.iCommentStringLength)
	{
		fwrite(pCommentString, 1, infoBlock.iCommentStringLength, pOutFile);
	}

	fwrite(&infoBlock, 1, sizeof(infoBlock), pOutFile);

	fclose(pOutFile);
	fileScanDirFreeNames(ppFileList);

	return infoBlock.iNumFiles;
}
	




//checks if a file has appended files. If it does, get its comment string into estring ppCommentString
bool FileHasAppendedFiles(char *pFileName, char **ppCommentString);

void ExtractAppendedFile(char *pFileName, char *pTargetDirectory);
void ExtractFileWithoutAppendedFiles(char *pInFileName, char *pOutFileName);
void TransferAppendedFiles(char *pFrom, char *pTo);