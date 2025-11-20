#pragma once

AUTO_ENUM;
typedef enum GfxStages
{
	GFX_ZPREPASS_EARLY, // Things to be outlined, or everything if zprepass is disabled
	GFX_OPAQUE_ONEPASS,	// Opaque objects not rendered in the zprepass
	GFX_OUTLINING_EARLY,// Use the depth prior to this to start outlining
	GFX_ZPREPASS_LATE,	// Things which do not want to be outlined
	GFX_AUX_VISUAL_PASS,
	GFX_SHADOW_BUFFER,
	GFX_CALCULATE_SCATTERING,
	GFX_OPAQUE_AFTER_ZPRE,
	GFX_LENSFLARE_ZO,
	GFX_APPLY_SCATTERING,
	GFX_DEFERRED_COMBINE,
	GFX_SKY,
	GFX_OUTLINING_LATE,
	GFX_DEPTH_OF_FIELD,
	GFX_NONDEFERRED_POST_OUTLINING,
	GFX_ALPHA_PREDOF,
	GFX_ALPHA,
	GFX_ALPHA_LOW_RES,
	GFX_SEPARATE_HDR_PASS,
	GFX_SHRINK_HDR,
	GFX_BLOOM,
	GFX_TONEMAP,
	GFX_WATER_DOF,
	GFX_WATER,
	GFX_RENDERSCALE,
	GFX_UI,
	GFX_UI_POSTPROCESS,

	GFX_NUM_STAGES,
} GfxStages;
extern StaticDefineInt GfxStagesEnum[];

typedef enum GfxBottleneck
{
	GfxBottleneck_GPUBound,
	GfxBottleneck_Misc,
	GfxBottleneck_Draw,
	GfxBottleneck_Queue,
	GfxBottleneck_QueueWorld,
	GfxBottleneck_AnimFX,
	GfxBottleneck_Networking,
	GfxBottleneck_UI,
	GfxBottleneck_MAX,
} GfxBottleneck;

typedef enum GfxActionType
{
	// Order of these are tied to max_actions_per_frame[]
	GfxAction_Unspecified, // Should never be used, just for error checking
	GfxAction_Primary,
	GfxAction_Headshot,
	GfxAction_ImpostorAtlas,
	GfxAction_MAX,
} GfxActionType;