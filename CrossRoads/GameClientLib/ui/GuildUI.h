#pragma once

#include "Guild.h"

AUTO_STRUCT;
typedef struct GuildRecruitData {
	ContainerID iContainerID;						AST(KEY)

	char *pcName;
	char *pcRecruitMessage;
	char *pcWebSite;

	const char *pcEmblem;						AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	U32 iEmblemColor0;
	U32 iEmblemColor1;
	F32 fEmblemRotation; // [-PI, PI)
	const char *pcEmblem2;						AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	U32 iEmblem2Color0;
	U32 iEmblem2Color1;
	F32 fEmblem2Rotation; // [-PI, PI)
	F32 fEmblem2X; // -100 to 100
	F32 fEmblem2Y; // -100 to 100
	F32 fEmblem2ScaleX;							AST(DEF(1.0f)) // 0 to 100
	F32 fEmblem2ScaleY;							AST(DEF(1.0f)) // 0 to 100
	const char *pcEmblem3;						AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary (Detail)

	U32 iColor1;
	U32 iColor2;

	int iMinLevelRecruit;
	char *pcRecruitCategories;

	int bHasMambers;

	const char *pcGuildAllegiance;				AST(POOL_STRING) // The guild allegiance
} GuildRecruitData;

void gclGuild_GameplayEnter(void);
void gclGuild_GameplayLeave(void);
