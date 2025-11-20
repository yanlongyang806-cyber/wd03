/***************************************************************************



***************************************************************************/
#pragma once

AUTO_STRUCT;
typedef struct HeadshotRequestParams
{
	char *pFileName;
	int iRequestID;
	U32 eContainerType;
	int iContainerID;
	char *pCostumeString;
	Vec3 size;
	char *pchStyle;
	int flags;
	char *pBGTextureName;
	char *pPoseString; 
	float animDelta;
	char *pchFrame;
	Vec3 camPos;
	Vec3 camDir;
	bool bForceBodyshot;
	bool bTransparent;
	bool bCostumeV0;
} HeadshotRequestParams;
