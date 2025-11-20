#pragma once

#include "GlobalTypes.h"
#include "httpJpegLibrary.h"

#define HEADSHOT_CRASHBEGAN_EVENT "HeadshotCrashBegan"
#define HEADSHOT_CRASHCOMPLETED_EVENT "HeadshotCrashCompleted"

typedef struct HeadshotRequestHandle
{
	int iRequestID;
	GlobalType eType;
	U32 iContainerID;
	int iImageSizeX;
	int iImageSizeY;
	char fileName[CRYPTIC_MAX_PATH];
	JpegLibrary_ReturnJpegCB *pJpegCB;
	void *pJpegUserData;
	Vec3 camPos;
	bool bGotCamPos;
	Vec3 camDir;
	bool bGotCamDir;
	float fAnimDelta;
	char pPoseInfo[256];
	char bgTexName[256];
	char achFrame[256];
	char achStyle[256];
	bool bForceBodyshot;
	bool bTransparent;
} HeadshotRequestHandle;

typedef struct TextureViewerRequestHandle
{
	char *pShortName; //just the texture name, no extension (ESTRING)
	int iRequestID;
	char fileName[CRYPTIC_MAX_PATH];
	JpegLibrary_ReturnJpegCB *pJpegCB;
	void *pJpegUserData;
	int iStartTime;
} TextureViewerRequestHandle;