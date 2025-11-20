#pragma once
GCC_SYSTEM

#include "GfxTextureEnums.h"
#include "WTex.h"

// Public editor exposure for TexOpts (just for the editors!)

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct TexOptList
{
	TexOpt **ppTexOpts; AST(NAME( "TexOpt") STRUCT(parse_tex_opt) REDUNDANT_STRUCT("FolderTexOpt", parse_folder_tex_opt))
} TexOptList;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndNinePatch");
typedef struct NinePatch
{
	const char *file_name; AST( CURRENTFILE )
	const char *texture_name; AST( POOL_STRING NO_TEXT_SAVE )
	U16 stretchableX[2]; // Inclusive positions of stretched segment
	U16 stretchableY[2];
	U16 paddingX[2];
	U16 paddingY[2];
} NinePatch;
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct NinePatchList
{
	NinePatch **ppNinePatches; AST(NAME("NinePatch"))
} NinePatchList;
extern ParseTable parse_NinePatchList[];
#define TYPE_parse_NinePatchList NinePatchList

const NinePatch *texGetNinePatch(const char *texname);

extern ParseTable parse_tex_opt[];
#define TYPE_parse_tex_opt tex_opt

