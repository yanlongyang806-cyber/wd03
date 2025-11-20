#pragma once
#include "wininclude.h"

typedef enum
{
	ANIMATIONLIBRARY,
	TEXTURELIBRARY,
	OBJECTLIBRARY,
	CHARACTERLIBRARY,

	MAX_ARTIST_TOOLS,

} enumArtistToolType;

typedef struct
{
	int iLaunchButtonID;
	int iExclamationPointID;
	int iArrowID;
	int iBGBCheckID;
	int iExtraCheckID;
} MCPArtistToolButtonIDs;

#if !STANDALONE

AUTO_STRUCT;
typedef struct ArtistToolStaticInfo
{
	int iToolNum;
	char *pDirectory;
	char *pCommandString;
	char *pExeName;
	char *pExtraString;
	char *pName;
} ArtistToolStaticInfo;

AUTO_STRUCT;
typedef struct ArtistToolDynamicInfo
{
	int iToolNum;
	bool bBGBChecked;
	bool bExtraChecked;
} ArtistToolDynamicInfo;

#endif



#define MAX_PRINTF_BUFFERS 8
#define MAX_PRINTF_BUFFER_LENGTH 500000

void LaunchArtistTool(int iWhichTool, bool bReLaunchIfRunning);
void KillAllArtistTools(void);
void UpdateArtToolPrintfBuffer(bool bForce);
void ResetArtToolButtonText(HWND hWnd, int iButtonNum);
void KillArtistTool(int iWhich);
void SetArtToolNeedsAttention(int iArtToolNum);
void FindArtistToolConsoleWindow(int iWhichTool);

//bits of this byte specify whether each individual art tool needs attention
extern char gArtToolsNeedAttention;


extern int artistToolPids[MAX_ARTIST_TOOLS];
extern char *gPrintfBuffers[MAX_PRINTF_BUFFERS];
extern bool gbPrintfBuffersChanged[MAX_PRINTF_BUFFERS];
extern int giActiveArtToolPrintBuffer;
extern HWND artistToolHWNDs[MAX_ARTIST_TOOLS];

