/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

AUTO_STRUCT;
typedef struct ClientGraphicsTextureAsset {
	char *pcTextureName;
	char *pcFilename;
	int count;
} ClientGraphicsTextureAsset;

AUTO_STRUCT;
typedef struct ClientGraphicsMaterialAsset {
	char *pcMaterialName;
	char *pcFilename;
	int count;
} ClientGraphicsMaterialAsset;

AUTO_STRUCT;
typedef struct ClientGraphicsLookupRequest {
	ClientGraphicsTextureAsset **eaTextures;
	ClientGraphicsMaterialAsset **eaMaterials;
} ClientGraphicsLookupRequest;

AUTO_STRUCT;
typedef struct AudioAssetComponents
{
	char *pcType;
	char **eaStrings;
	U32 uiNumData;
	U32 uiNumDataWithAudio;
}
AudioAssetComponents;

AUTO_STRUCT;
typedef struct AudioAssets
{
	AudioAssetComponents **eaComponents;
}
AudioAssets;

typedef void (*cmdClientReport_AudioAssets_Callback)(const char **, const char ***, U32 *, U32 *);
cmdClientReport_AudioAssets_Callback reportAudioAssets_AiCivilian_Callback;