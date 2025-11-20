#pragma once
GCC_SYSTEM

#include "wlVolumes.h"
#include "WorldLibEnums.h"
#include "referencesystem.h"
#include "wlLight.h"

typedef struct SkyInfoGroup SkyInfoGroup;
typedef struct SkyInfoGroupInstantiated SkyInfoGroupInstantiated;
typedef struct SkyInfoOverride SkyInfoOverride;
typedef struct Frustum Frustum;
typedef struct Model Model;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct MaterialNamedTexture MaterialNamedTexture;
typedef struct GfxCameraView GfxCameraView;
typedef struct Material Material;
typedef struct GeoRenderInfo GeoRenderInfo;
typedef struct StarField StarField;
typedef struct StarData StarData;
typedef struct WorldAtmosphereProperties WorldAtmosphereProperties;
typedef struct BasicTexture BasicTexture;
typedef struct SkyInfo SkyInfo;

#define GFX_SKY_DICTIONARY "SkyInfo"

#define DOF_LEAVE (-1.f)
#define MAX_SKY_DOMES 32

typedef enum DOFSwitchType {
	DOFS_SNAP,
	DOFS_SMOOTH,
} DOFSwitchType;

typedef struct SkyDrawable
{
	// for sky domes:
	Model		*model;

	// for starfields:
	Material	*material;
	StarData	**star_data;
	GeoRenderInfo *geo_render_info;
	StarField	*star_field;

	// for atmosphere:
	WorldAtmosphereProperties *atmosphere;

	// for both:
	Mat4		mat;
} SkyDrawable;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyDomeTime);
typedef struct SkyDomeTime
{
	F32			time;						AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )
	F32			alpha;						AST( NAME(Alpha) WIKI("Alpha value (So you can make clouds partially fade at night)") )	
	F32			scale;						AST( NAME(Scale) DEF(0.01f) WIKI("Scale value (So you can shrink or grow a moon or a sun)") )
	F32			angle;						AST( NAME(Angle) WIKI("Angle around the RotationAxis that the Sky Dome has Rotated") )
	Vec3		tintHSV;					AST( NAME(TintHSV) FORMAT_HSV WIKI("Tint ") )
	Vec3		pos;						AST( NAME(Position) WIKI("XYZ Center Position of the SkyDome") )
	Vec3		ambientHSV;					AST( NAME(AmbientHSV) FORMAT_HSV WIKI("Ambient color to use on the Sky Dome") )
	F32			ambient_weight;				AST( NAME(AmbientWeight) WIKI("Internal value") )
	MaterialNamedConstant	**mat_props;	AST( NAME(MaterialProperty) WIKI("Material op name + RGBA") )

	U32	bfParamsSpecified[1];				AST( USEDFIELD )
} SkyDomeTime;
extern ParseTable parse_SkyDomeTime[];
#define TYPE_parse_SkyDomeTime SkyDomeTime

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndStarField);
typedef struct StarField
{
	U32		seed;					AST( NAME(StarSeed) WIKI("The random seed for this star field.") )
	U32		count;					AST( NAME(StarCount) DEF(100) WIKI("The number of stars in this star field.") )
	F32		size_min;				AST( NAME(StarSizeMin) DEF(0.3f) WIKI("The min size of stars, in percent of screen space (at fov 55 or current fov, depending on the value of ScaleWithFov).") )
	F32		size_max;				AST( NAME(StarSizeMax) DEF(0.5f) WIKI("The max size of stars, in percent of screen space (at fov 55 or current fov, depending on the value of ScaleWithFov).") )
	Vec3	color_min;				AST( NAME(StarColorHSVMin) FORMAT_HSV WIKI("The min color of stars in HSV space.") )
	Vec3	color_max;				AST( NAME(StarColorHSVMax) FORMAT_HSV WIKI("The max color of stars in HSV space.") )
	bool	random_rotation;		AST( NAME(UseRandomRotation) DEF(true) WIKI("Randomly rotate the star sprites.") )
	bool	scale_with_fov;			AST( NAME(ScaleWithFov) DEF(true) WIKI("Star size should be interpreted as percent of screen space.") )

	Vec3	slice_axis;				AST( NAME(SliceAxis) WIKI("Axis around which to place stars.") )
	F32		slice_angle;			AST( NAME(SliceAngle) DEF(180) WIKI("Angle for the slice to place stars in (in degrees, 0-90).") )
	F32		slice_fade;				AST( NAME(SliceFade) DEF(0) WIKI("Power to fade the stars out at the edges of the slice, 0 to disable fading.") )

	bool	half_dome;				AST( NAME(HalfDome) WIKI("Only put stars in the top half of the sky.") )

	U32	bfParamsSpecified[1];		AST( USEDFIELD )

} StarField;

extern ParseTable parse_StarField[];
#define TYPE_parse_StarField StarField

#define MAX_FLARES	8

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndLensFlarePiece);
typedef struct LensFlarePiece
{
	char		*material_name;			AST( NAME(MaterialName) WIKI("Name of material for the flare") )
	char		*texture_name;			AST( NAME(TextureName) WIKI("Name of texture for the flare") )
	F32			size;					AST( NAME(Size) WIKI("Screen size scale of the flare piece. 1 indicates the width of the screen, 0 indicates no extent and intermediate values scale between these values.") )
	Vec3		hsv_color;				AST( NAME(Color) FORMAT_HSV WIKI("Color tint.") )
	F32			position;				AST( NAME(Offset) WIKI("Relative position along the line between the light source on screen and the center of the screen. 0 indicates at the light source, 1 indicates the point opposite the light source across the center of the screen, and intermediate values indicate positions along the line. Values outside [0,1] indicate values past either end of the line.") )

	Material		*material;	NO_AST
	BasicTexture	*texture;	NO_AST
} LensFlarePiece;

extern ParseTable parse_LensFlarePiece[];
#define TYPE_parse_LensFlarePiece LensFlarePiece

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndLensFlare);
typedef struct LensFlare
{
	const char			*name;				AST( NAME(Name) WIKI("Name of the Lens Flare") )
	LensFlarePiece	**flares;				AST( NAME(LensFlarePiece) WIKI("Defines set of flare sprites") )
} LensFlare;

extern ParseTable parse_LensFlare[];
#define TYPE_parse_LensFlare LensFlare

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndScattering);
typedef struct SkyTimeScattering
{
	F32			time;					AST( NAME(Time) WIKI("SkyTimeScattering of day (0 - 24) at which this SkyTime applies") )
	Vec3		scatterParameters;		AST( NAME(Parameters) WIKI("Rate of light increase per unit distance when in lit area. Rate of light decrease per unit distance due to scattering media. Scale factor for scatter brightness.") )
	Vec3		scatterLightColorHSV;	AST( NAME(LightColor) WIKI("Color (HSV) of light due to scattering.") )
} SkyTimeScattering;

extern ParseTable parse_SkyTimeScattering[];
#define TYPE_parse_SkyTimeScattering SkyTimeScattering

AUTO_ENUM;
typedef enum SkyDomeType {
	SDT_Object,
	SDT_Luminary,
	SDT_Luminary2,
	SDT_StarField,
	SDT_Atmosphere,
} SkyDomeType;
extern StaticDefineInt SkyDomeTypeEnum[];

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyDome);
typedef struct SkyDome
{
	char		*name;					AST( NAME(Name) WIKI("Name of the geometry to draw, or the material if this is a star field") )
	char		*display_name;			AST( NAME(DisplayName) WIKI("Name used in the editor for orginization.") )
	F32			sort_order;				AST( NAME(SortOrder) WIKI("Relative sort offset versus other SkyDome entries. Larger values make the dome draw earlier.") )			
	const char*	sky_uid;				NO_AST//Unique ID (POOL_STRING) for this sky_dome, used when blending

	LensFlare	*lens_flare;			AST( NAME(LensFlare) WIKI("Lens flare parameters.") )

	StarField	*star_field;			AST( NAME(StarField) WIKI("Star field parameters.") )

	WorldAtmosphereProperties *atmosphere;	AST( NAME(Atmosphere) WIKI("Atmosphere parameters.") )
	Vec3		atmosphereSunHSV;			AST( NAME(AtmosphereSunHSV) FORMAT_HSV WIKI("Diffuse lighting value for the sun used to light the atmosphere.") )

	MaterialNamedTexture	**tex_swaps;	AST( NAME(MaterialTexture) WIKI("Material op name + texture name") )

	SkyDomeTime **dome_values;			AST( NAME(SkyDomeTime) WIKI("Values for the Sky Dome through out the day.") )
	SkyDomeTime current_dome_values;	NO_AST	//Blended to current time.

	bool		luminary;				AST( NAME(Luminary) WIKI("Luminaries are placed in the sky like a moon or a sun and emit light.") )
	bool		luminary2;				AST( NAME(SecondaryLuminary) WIKI("Luminaries are placed in the sky like a moon or a sun and emit light. Secondary Luminary uses secondary light data.") )
	bool		character_only;			AST( NAME(CharacterOnly) WIKI("Makes the light coming from this luminary apply only to characters.") )
	Vec3		rotation_axis;			AST( NAME(RotationAxis) WIKI("Axis that the Sky Dome rotates around.") )
	bool		pos_specified;			NO_AST
	U32			motion_loop_cnt;		AST( NAME(NumberOfLoops) WIKI("How many times will this object travel through the sky per day.") )
	Vec3		start_pos;				AST( NAME(StartPos) WIKI("Start postion for each loop.") )
	Vec3		end_pos;				AST( NAME(EndPos) WIKI("End Positon for each loop.") )
	F32			loop_fade_percent;		AST( NAME(LoopFadePercent) WIKI("Percent of the loop at the begining and end for which to fade in and out.") )

	bool		sunlit;					AST( NAME(SunLit) WIKI("Apply the sun as lighting to this sky dome.") )

	bool		highDetail;				AST( NAME(HighDetail) WIKI("Only draw this sky dome if visscale is set above 0.6") )

	U32	bfParamsSpecified[1];			AST( USEDFIELD )

	// Don't blend/render a SkyDome that is flagged with errors
	bool bHasErrors;					NO_AST

} SkyDome;
extern ParseTable parse_SkyDome[];
#define TYPE_parse_SkyDome SkyDome

typedef struct BlendedSkyDome
{
	bool is_refrence;			//If true, then "dome" should not be freed as it is just a reference to a dome
	SkyDome *dome;
	F32 alpha;
	F32 group_percent;
	F32 sort_order;
	bool high_detail;
	SkyDrawable	*drawable;
}BlendedSkyDome;

//MOONS ARE NO LONERGER USED
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndMoon);
typedef struct Moon
{
	char *name;						AST( NAME(Name) STRUCTPARAM WIKI("Name of the geometry to draw") )
	bool use_default;				AST( NAME(UseDefault) WIKI("Whether or not to use the default/auto-generated speed/rotation values") )
	F32 speed;						AST( NAME(Speed) WIKI("Speed at which the celestial body moves across the sky") )
	Vec3 pyr;						AST( NAME(PYR) WIKI("Rotational PYR") )
	// Should this be an embedded struct?
	SkyDrawable *drawable;			NO_AST
	SkyDome *moon_sky_dome;			NO_AST //This moon affects which skydome (just a reference, do not free this)

	U32		bfParamsSpecified[1];	AST( USEDFIELD )
} Moon;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkySun) AST_IGNORE(MoonScales) AST_IGNORE(MoonAlphas) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeSun
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	//BTH: following line needs to be removes before final update.
	F32		lightRange;				AST( NAME(LightRange) WIKI("The maximum brightness that should be exposed.") )
	Vec3	ambientHSV;				AST( NAME(AmbientHSV) FORMAT_HSV WIKI("Ambient lighting color") )
	Vec3	ambientHSValternate;	AST( NAME(AmbientHSVAlternate) FORMAT_HSV WIKI("Alternate ambient lighting color") )
	Vec3	skyLightHSV;			AST( NAME(SkyLightHSV) FORMAT_HSV WIKI("Sky hemisphere lighting color") )
	Vec3	groundLightHSV;			AST( NAME(GroundLightHSV) FORMAT_HSV WIKI("Ground hemisphere lighting color") )
	Vec3	sideLightHSV;			AST( NAME(SideLightHSV) FORMAT_HSV WIKI("Side lighting color") )
	Vec3	diffuseHSV;				AST( NAME(DiffuseHSV) FORMAT_HSV WIKI("Diffuse color for the sun") )
	Vec3	specularHSV;			AST( NAME(SpecularHSV) FORMAT_HSV WIKI("Specular color for the sun") )
	Vec3	secondaryDiffuseHSV;	AST( NAME(SecondaryDiffuseHSV) FORMAT_HSV WIKI("Secondary (back) color for the sun") )
	Vec3	shadowColorHSV;			AST( NAME(ShadowColorHSV) FORMAT_HSV WIKI("Shadow color for the sun's shadows (only in some projects)") )
	F32		shadowMinValue;			AST( NAME(ShadowMinValue) WIKI("Minimum value for the sun's shadows (0 = fully shadowed, 1 = fully lit)") )
	Vec3	backgroundColorHSV;		AST( NAME(BackgroundColorHSV) FORMAT_HSV WIKI("Background clear color") )
	U32		bDisabled;				AST( NAME(Disabled) WIKI("Completely disabled (for making outdoor maps look like indoor maps)") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

}SkyTimeSun;
extern ParseTable parse_SkyTimeSun[];
#define TYPE_parse_SkyTimeSun SkyTimeSun

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkySecondarySun) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeSecondarySun
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	Vec3	diffuseHSV;				AST( NAME(DiffuseHSV) FORMAT_HSV WIKI("Diffuse color for the sun") )
	Vec3	specularHSV;			AST( NAME(SpecularHSV) FORMAT_HSV WIKI("Specular color for the sun") )
	Vec3	secondaryDiffuseHSV;	AST( NAME(SecondaryDiffuseHSV) FORMAT_HSV WIKI("Secondary (back) color for the sun") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

}SkyTimeSecondarySun;
extern ParseTable parse_SkyTimeSecondarySun[];
#define TYPE_parse_SkyTimeSecondarySun SkyTimeSecondarySun

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyCharacterLighting) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeCharacterLighting
{
	F32		time;						AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	Vec3	ambientHSVOffset;			AST( NAME(AmbientHSVOffset) FORMAT_HSV_OFFSET WIKI("Character ambient lighting color offset") )
	Vec3	skyLightHSVOffset;			AST( NAME(SkyLightHSVOffset) FORMAT_HSV_OFFSET WIKI("Character sky hemisphere lighting color offset") )
	Vec3	groundLightHSVOffset;		AST( NAME(GroundLightHSVOffset) FORMAT_HSV_OFFSET WIKI("Character ground hemisphere lighting color offset") )
	Vec3	sideLightHSVOffset;			AST( NAME(SideLightHSVOffset) FORMAT_HSV_OFFSET WIKI("Character side lighting color offset") )
	Vec3	diffuseHSVOffset;			AST( NAME(DiffuseHSVOffset) FORMAT_HSV_OFFSET WIKI("Character diffuse lighting color offset") )
	Vec3	specularHSVOffset;			AST( NAME(SpecularHSVOffset) FORMAT_HSV_OFFSET WIKI("Character specular lighting color offset") )
	Vec3	secondaryDiffuseHSVOffset;	AST( NAME(SecondaryDiffuseHSVOffset) FORMAT_HSV_OFFSET WIKI("Character secondary (back) lighting color offset") )
	Vec3	shadowColorHSVOffset;		AST( NAME(ShadowColorHSVOffset) FORMAT_HSV_OFFSET WIKI("Character shadow color offset") )
	Vec3	backlightHSV;				AST( NAME(BacklightHSV) FORMAT_HSV WIKI("Character backlight color") )

	U32		bfParamsSpecified[1];		AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

}SkyTimeCharacterLighting;
extern ParseTable parse_SkyTimeCharacterLighting[];
#define TYPE_parse_SkyTimeCharacterLighting SkyTimeCharacterLighting

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyCloudShadows) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeCloudShadows
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	F32		layer1Multiplier;		AST( NAME(Layer1Multiplier) WIKI("Projected shadow texture multiplier for cloud layer 1.") )
	F32		layer1Scale;			AST( NAME(Layer1Scale) WIKI("Size of the projected shadow texture in feet for cloud layer 1.") )
	Vec2	layer1ScrollRate;		AST( NAME(Layer1ScrollRate) WIKI("Rate at which the projected shadow texture moves in feet per second for cloud layer 1.") )

	F32		layer2Multiplier;		AST( NAME(Layer2Multiplier) WIKI("Projected shadow texture multiplier for cloud layer 2.") )
	F32		layer2Scale;			AST( NAME(Layer2Scale) WIKI("Size of the projected shadow texture in feet for cloud layer 2.") )
	Vec2	layer2ScrollRate;		AST( NAME(Layer2ScrollRate) WIKI("Rate at which the projected shadow texture moves in feet per second for cloud layer 2.") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

} SkyTimeCloudShadows;
extern ParseTable parse_SkyTimeCloudShadows[];
#define TYPE_parse_SkyTimeCloudShadows SkyTimeCloudShadows

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyShadowFade) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeShadowFade
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	F32		fadeValue;				AST( NAME(FadeValue) WIKI("Shadow value to fade to when turning cloudy to hide the sun angle transitions (0 = fully shadows, 1 = fully lit)") )
	F32		fadeTime;				AST( NAME(FadeTime) WIKI("Time it takes to fade in the ShadowFadeValue when hiding the sun angle transitions.") )
	F32		darkTime;				AST( NAME(DarkTime) WIKI("Amount of time to stay at the ShadowFadeValue when hiding the sun angle transitions.") )
	F32		pulseAmount;			AST( NAME(PulseAmount) WIKI("How far towards the ShadowFadeValue to pulse the sun shadows (0 = normal shadows, 1 = ShadowFadeValue).") )
	F32		pulseRate;				AST( NAME(PulseRate) WIKI("How fast to pulse towards the ShadowFadeValue.") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

} SkyTimeShadowFade;
extern ParseTable parse_SkyTimeShadowFade[];
#define TYPE_parse_SkyTimeShadowFade SkyTimeShadowFade

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndLevels) AST_STRIP_UNDERSCORES;
typedef struct SkyColorLevels
{
	Vec2 input_range;				AST( NAME(InputRange) )
	Vec2 output_range;				AST( NAME(OutputRange) )
	F32 gamma;						AST( NAME(Gamma) )
} SkyColorLevels;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndCurve) AST_STRIP_UNDERSCORES;
typedef struct SkyColorCurve
{
	Vec2 control_points[4];			AST( AUTO_INDEX(Point) )
} SkyColorCurve;
extern ParseTable parse_SkyColorCurve[];
#define TYPE_parse_SkyColorCurve SkyColorCurve

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndCurve) AST_STRIP_UNDERSCORES;
typedef struct SkyColorCurveHSV
{
	F32 control_points[4];			AST( AUTO_INDEX(Point) )
	Vec3 hsv_vals[4];				AST( AUTO_INDEX(PointHSV) FORMAT_HSV )
} SkyColorCurveHSV;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyColorCorrection) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeColorCorrection
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	SkyColorCurve curve_intensity;	AST( NAME(Curve) WIKI("Sets the curve adjustment for intensity.") )
	SkyColorLevels levels_intensity; AST( NAME(Levels) WIKI("Sets the levels adjustments for intensity.") )

	SkyColorCurve curve_red;		AST( NAME(CurveRed) WIKI("Sets the curve adjustment for the red channel.") )
	SkyColorCurve curve_green;		AST( NAME(CurveGreen) WIKI("Sets the curve adjustment for the green channel.") )
	SkyColorCurve curve_blue;		AST( NAME(CurveBlue) WIKI("Sets the curve adjustment for the blue channel.") )
	Vec3 color_curve_multi;			AST( NAME(ColorCurveMulti) WIKI("Scales the output values for the red, green, and blue curves.") )

	SkyColorLevels levels_red;		AST( NAME(LevelsRed) WIKI("Sets the levels adjustments for the red channel.") )
	SkyColorLevels levels_green;	AST( NAME(LevelsGreen) WIKI("Sets the levels adjustments for the green channel.") )
	SkyColorLevels levels_blue;		AST( NAME(LevelsBlue) WIKI("Sets the levels adjustments for the blue channel.") )

	SkyColorCurve saturation_curve;	AST( NAME(SaturationCurve) WIKI("Sets the curve adjustment for saturation.") )

	SkyColorCurveHSV tint_curve;	AST( NAME(TintCurve) WIKI("Sets the curve for intensity based tinting.") )

	F32		local_contrast_scale;	AST( NAME(LocalContrast) WIKI("Sets the amount of local contrast adjustment.") )
	F32		unsharp_amount;			AST( NAME(UnsharpAmount) WIKI("Sets the amount of unsharp masking (alternative/additional local contrast adjustment).") )
	F32		unsharp_threshold;		AST( NAME(UnsharpThreshold) WIKI("Sets the minimum difference require to apply unsharp masking (reduces some artifacts from Unsharp Masking on smooth surfaces).") )

	F32		specificOverlap;		AST( NAME(ScreenTintSpecificOverlap) WIKI("Correction per color overlap between specific hues.") )
	F32		specificHue[6];			AST( NAME(ScreenTintSpecificHues) WIKI("Hue correction per color.") )
	F32		specificSaturation[6];	AST( NAME(ScreenTintSpecificSaturations) WIKI("Saturation correction per color.") )
	F32		specificValue[6];		AST( NAME(ScreenTintSpecificValues) WIKI("Value correction per color.") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

	// Everything below this point is for sky editor use only
	// Whether or not "simple" mode is active should already be determined according to the override value
	F32		masterSaturation;		AST( NAME(MasterSaturation) WIKI("Master saturation.") )
	F32		masterSaturationBias;	AST( NAME(MasterSaturationBias) WIKI("Master saturation bias.") )
	F32		masterValue;			AST( NAME(MasterValue) WIKI("Max master brightness.") )
	F32		masterContrast;			AST( NAME(MasterContrast) WIKI("Master contrast.") )
	F32		masterBias;				AST( NAME(MasterBalance) WIKI("master balance.") )

	Vec3	colorValue;			AST( NAME(ColorValue) WIKI("Max brightness of color channels.") )
	Vec3	colorContrast;		AST( NAME(ColorContrast) WIKI("Color contrast.") )
	Vec3	colorBias;			AST( NAME(ColorBalance) WIKI("Color balance.") )

} SkyTimeColorCorrection;
extern ParseTable parse_SkyTimeColorCorrection[];
#define TYPE_parse_SkyTimeColorCorrection SkyTimeColorCorrection

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyTint) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeTint
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	Vec3	screenTintHSV;			AST( NAME(ScreenTintHSV) FORMAT_HSV WIKI("Fullscreen color adjustment in HSV. Scales each H, S, and V value.") )
	Vec3	screenTintOffsetHSV;	AST( NAME(ScreenTintOffsetHSV) FORMAT_HSV WIKI("Fullscreen color adjustment in HSV. Adds to H, S, and V value.") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

} SkyTimeTint;
extern ParseTable parse_SkyTimeTint[];
#define TYPE_parse_SkyTimeTint SkyTimeTint

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyOutline) AST_STRIP_UNDERSCORES AST_IGNORE(OutlineAdaptFadeOffset) AST_IGNORE(OutlineAdaptFadeScale) AST_IGNORE(OutlineAdaptFadeMin) AST_IGNORE(OutlineAdaptFadeSaturation) ;
typedef struct SkyTimeOutline
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	Vec3	outlineHSV;				AST( NAME(OutlineHSV) FORMAT_HSV WIKI("Outline color at luminance1") )
	F32		outlineAlpha;			AST( NAME(OutlineAlpha) WIKI("Outline transparency") )
	Vec4	outlineFade;			AST( NAME(OutlineFade) WIKI("Outline fade depths: Start fade Z for depth-test, end fade Z. Start fade Z for normal test, end fade Z. ") )

	F32		outlineLightRange1;		AST( NAME(OutlineLuminance1) WIKI("Scene luminance at which to use the first outline color") )
	F32		outlineLightRange2;		AST( NAME(OutlineLuminance2) WIKI("Scene luminance at which to use the second outline color") )
	Vec3	outlineHSV2;			AST( NAME(OutlineHSV2) FORMAT_HSV WIKI("Outline color at luminance2") )

	F32		outlineThickness;		AST( NAME(OutlineThickness) WIKI("Scale the thickness by this amount") DEFAULT(1) )

	bool	useZ;					AST( NAME(UseZ) WIKI("Calculate outlines in linear Z-space") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

} SkyTimeOutline;
extern ParseTable parse_SkyTimeOutline[];
#define TYPE_parse_SkyTimeOutline SkyTimeOutline

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyFog) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeFog
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	Vec3	lowFogColorHSV;			AST( NAME(FogColorHSV) FORMAT_HSV WIKI("Fog color to be used at low heights") )
	Vec3	highFogColorHSV;		AST( NAME(HighFogColorHSV) FORMAT_HSV WIKI("Fog color to be used at high heights") )
	Vec3	clipFogColorHSV;		AST( NAME(ClipFogColorHSV) FORMAT_HSV WIKI("Fog color to be used when fog clipping is on") )
	Vec3	clipBackgroundColorHSV;	AST( NAME(ClipBackgroundColorHSV) FORMAT_HSV WIKI("Background color to be used when fog clipping is on") )
	F32		clipFogDistanceAdjust;	AST( NAME(ClipFogDistanceAdjust) WIKI("Adjustment applied to fogClipDist in low-end modes (0 = default, 2 = 200% larger distance)") )
	Vec2	lowFogDist;				AST( NAME(FogDist) WIKI("Low fog distance (near, far)") )
	Vec2	highFogDist;			AST( NAME(HighFogDist) WIKI("High fog distance (near, far)") )
	F32		lowFogMax;				AST( NAME(FogMax) WIKI("The maximum amount of fog allowed for the low fog (0 - 1)") )
	F32		highFogMax;				AST( NAME(HighFogMax) WIKI("The maximum amount of fog allowed for the high fog (0 - 1)") )
	Vec2	fogHeight;				AST( NAME(FogHeight) WIKI("Fog heights (low, high) that determine which fog color and distance to use") )
	
	Vec2	fogDensity;				AST( NAME(FogDensity) WIKI("Fog light extinction rate, ambient scattering rate") )
	Vec3	volumeFogPos;			AST( NAME(VolumeFogPos) WIKI("Center of volumetric fog") )
	Vec3	volumeFogScale;			AST( NAME(VolumeFogScale) WIKI("Radii of volumetric fog ellipsoid") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

} SkyTimeFog;
extern ParseTable parse_SkyTimeFog[];
#define TYPE_parse_SkyTimeFog SkyTimeFog

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyLightBehavior) AST_STRIP_UNDERSCORES AST_IGNORE(ExposureMin) AST_IGNORE(ExposureMax) AST_IGNORE(LuminanceRange);
typedef struct SkyTimeLightBehavior
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	F32		lightRange;				AST( NAME(LightRange) WIKI("The maximum brightness that should be exposed.") )
	F32		lightAdaptationAmount;	AST( NAME(LightAdaptation) WIKI("How much to use the sampled scene luminance to do light adaptation; 0 turns light adaptation off and 1 turns it completely on.") )
	F32		lightAdaptationRate;	AST( NAME(LightAdaptationRate) WIKI("Specifies the amount of time (in seconds) the HDR system will take to adjust the light range by 1.") )
	F32		exposure;				AST( NAME(Exposure) WIKI("Modifies the grey point of the light adaptation.  A value of 0.5 is no change; lower values darken the scene and higher values brighten the scene.") )
	F32		blueshiftMin;			AST( NAME(BlueshiftMin) WIKI("Specifies the luminance value at which colors should be fully desaturated.") )
	F32		blueshiftMax;			AST( NAME(BlueshiftMax) WIKI("Specifies the luminance value at which colors should start desaturating.  At luminance values above this value colors will be fully saturated.") )
	Vec3	blueshiftHSV;			AST( NAME(BlueshiftHSV) FORMAT_HSV WIKI("Specifies the HSV color to which colors in the blueshift range should shift towards.") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

} SkyTimeLightBehavior;
extern ParseTable parse_SkyTimeLightBehavior[];
#define TYPE_parse_SkyTimeLightBehavior SkyTimeLightBehavior

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSkyBloom) AST_STRIP_UNDERSCORES  AST_IGNORE(LensName) AST_IGNORE(BloomHighpass) AST_IGNORE(BloomAmount) AST_IGNORE(BloomBlurSize);
typedef struct SkyTimeBloom
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	F32		bloomOffsetValue;		AST( NAME(BloomOffsetValue) WIKI("The brightness past the light range at which bloom starts.") )
	F32		bloomRate;				AST( NAME(BloomRate) WIKI("The rate at which bloom gets brighter.  0 to disable bloom.") )
	F32		bloomRange;				AST( NAME(BloomRange) WIKI("The range of brightnesses that can bloom.  Affects the bloom value precision.") )
	Vec3	bloomBlurAmount;		AST( NAME(BloomBlurAmount) WIKI("Specifies the multiplier for each blur level (high, medium, low) to be added together into the bloom texture.") )

	F32		lowQualityBloomStart;	AST( NAME(LowQualityBloomStart) WIKI("The brightness (in screen brightness) at which bloom starts for low quality bloom.") )
	F32		lowQualityBloomMultiplier;	AST( NAME(LowQualityBloomMultiplier) WIKI("Brightness multiplier applied after LowQualityBloomStart is subtracted from the screen brightness.") )
	F32		lowQualityBloomPower;	AST( NAME(LowQualityBloomPower) WIKI("The exponent to which the offset and multiplied brightness is raised before getting downsampled and blurred.") )

	F32		glareAmount;			AST( NAME(GlareAmount) WIKI("Multiplier for glare when it is added to the final image.  0 to disable glare.") )

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )

} SkyTimeBloom;
extern ParseTable parse_SkyTimeBloom[];
#define TYPE_parse_SkyTimeBloom SkyTimeBloom

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndAmbientOcclusion)  AST_IGNORE(PixelSampleRadius) AST_IGNORE(PixelSampleFalloff) AST_IGNORE(PixelSampleScale) AST_STRIP_UNDERSCORES;
typedef struct SkyTimeAmbientOcclusion
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )

	F32 	worldSampleRadius;		AST( NAME(WorldSampleRadius) WIKI("The radius in feet to sample within for the world-space based SSAO term.") )
	F32 	worldSampleFalloff;		AST( NAME(WorldSampleFalloff) WIKI("The falloff rate for the world-space based SSAO term.") )
	F32 	worldSampleScale;		AST( NAME(WorldSampleScale) WIKI("The scale multiplier for the world-space based SSAO term.") )

	F32 	overallScale;			AST( NAME(OverallScale) WIKI("The overall scale for the SSAO term.") )
	F32 	overallOffset;			AST( NAME(OverallOffset) WIKI("The overall offset for the SSAO term (subtracted from the SSAO term.") )

	F32		litAmount;				AST (NAME(LitAmount) WIKI("The amount of SSAO used for non-ambient light") DEFAULT(0.5))

	U32		bfParamsSpecified[1];	AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )
	U32		bfParamsSize;			NO_AST

} SkyTimeAmbientOcclusion;
extern ParseTable parse_SkyTimeAmbientOcclusion[];
#define TYPE_parse_SkyTimeAmbientOcclusion SkyTimeAmbientOcclusion

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndWind") AST_STRIP_UNDERSCORES;
typedef struct SkyTimeWind
{
	F32  time;						AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies") )
	F32	 speed;						AST( NAME(Speed) DEFAULT(0) WIKI("The speed of the wind") )
	F32	 speedVariation;			AST( NAME(SpeedVariation) DEFAULT(0) WIKI("The amount of variation in the speed of the wind.") )
	Vec3 direction;					AST( NAME(Direction) WIKI("The direction of wind.") )
	Vec3 directionVariation;		AST( NAME(DirectionVariation) WIKI("The amount of variation in the direction of wind.") )
	//this was initially misspelled but I left the mistake as an alternate name to avoid breaking any files - LDM
	F32  turbulence;				AST( NAME(Turbulence, Turbulance) DEFAULT(0.5f) WIKI("The speed at which the wind direction and speed changes.") )
	F32  variationScale;			AST( NAME(variationScale) DEFAULT(0.03) WIKI("The amount world units are scaled before they are used as input to the random noise generator") )
} SkyTimeWind;
extern ParseTable parse_SkyTimeWind[];
#define TYPE_parse_SkyTimeWind SkyTimeWind

//Flag to check if a list of value has been specified inside a SkyInfo or BlendedSkyInfo
typedef enum SkyValuesSpecifiedFlag
{
	SKY_SUN_VALUES				= (1<<0),
	SKY_CLOUD_SHADOW_VALUES		= (1<<1),
	SKY_SHADOW_FADE_VALUES		= (1<<2),
	SKY_TINT_VALUES				= (1<<3),
	SKY_OUTLINE_VALUES			= (1<<4),
	SKY_FOG_VALUES				= (1<<5),
	SKY_TONE_MAPPING_VALUES		= (1<<6),
	SKY_BLOOM_VALUES			= (1<<7),
	SKY_DOF_VALUES				= (1<<8),
	SKY_DOME_VALUES				= (1<<9),
	SKY_OCCLUSION_VALUES		= (1<<10),
	SKY_CHAR_LIGHT_VALUES		= (1<<11),
	SKY_COLOR_CORRECTION_VALUES	= (1<<12),
	SKY_WIND_VALUES				= (1<<13),
	SKY_SCATTERING_VALUES		= (1<<14),
	SKY_SIMPLE_CC_VALUES		= (1<<15),
	SKY_SCND_SUN_VALUES			= (1<<16),
	SKY_SHADOW_VALUES			= (1<<17),
} SkyValuesSpecifiedFlag;

// All values that should never be set together should be paired with each other in this variable located in SkyEditor.c
extern IVec2 SkyExclusiveOverrides[];

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSky) AST_STRIP_UNDERSCORES AST_IGNORE(FogHeightRange) AST_IGNORE(FogRampColor) AST_IGNORE(FogRampDistance) WIKI("Sky File Doc");
typedef struct SkyInfo
{
	const char	*filename;							AST( NAME(FN) CURRENTFILE) // Must be first for ParserReloadFile
	const char	*filename_no_path;					AST( NAME(FNNoPath) KEY POOL_STRING )
	bool bFilenameNoPathShouldNotBeOverwritten;		AST( NAME(FNNoPathShouldNotBeOverwritten) )		// filename_no_path contains the true reference name and
																									// should not be overwritten by filename in gfxSkyFixupFunc
																									// in GfxSky.c. Makes UGC FakeSky stuff not error on preview
																									// (see COR-16241)
		 
	const char	*scope;								AST( NAME(Scope) POOL_STRING )
	char		*tags;								AST( NAME(Tags) )
	char		*notes;								AST( NAME(Notes) )
	REF_TO(SkyInfo) noPPFallback;					AST( NAME(NoPPFallback) )

	Moon	**moons;								AST( NAME(Moon) WIKI("A sun, moon, or other celestial body.  The first moon specified is assumed to be the sun and is used for determining global light direction") )

	bool	ignoreFogClipFar;						AST( NAME(IgnoreFogClip) DEFAULT(0) WIKI("Ignore this sky's Fog Clip Far setting.") )
	bool	fogClipFar;								AST( NAME(FogClip) WIKI("Clip all objects beyond the fog distance.  Automatically sets the background color equal to the fog color.") )
	bool	ignoreFogClipLow;						AST( NAME(IgnoreFogClipLow) DEFAULT(0) WIKI("Ignore this sky's Fog Clip low setting.") )
	bool	fogClipLow;								AST( NAME(FogClipLow) WIKI("Clip all objects beyond the fog distance if they are below the camera.  Automatically sets the background color equal to the fog color.") )

	const char* cloudShadowTexture;					AST( NAME(CloudShadowTexture) POOL_STRING WIKI("The name of the projected shadow texture to use for all cloud layers.") )
	const char* diffuseWarpTextureCharacter;		AST( NAME(DiffuseWarpTextureCharacter) POOL_STRING WIKI("The name of the diffuse warping texture to use for character lighting.") )
	const char* diffuseWarpTextureWorld;			AST( NAME(DiffuseWarpTextureWorld) POOL_STRING WIKI("The name of the diffuse warping texture to use for world lighting.") )
	const char* ambientCube;						AST( NAME(AmbientCube) POOL_STRING WIKI("The name of the ambient cubemap texture to use for ambient lighting on the materials that want it.") )
	const char* reflectionCube;						AST( NAME(ReflectionCube) POOL_STRING WIKI("The name of the reflection cubemap texture to use if not specified in the region or material.") )

	S32 skyOverrideValue;							AST( NAME(SkyOverride) WIKI("Value stating which block this sky file is overriding.") )

	SkyTimeSun **sunValues;							AST( NAME(SkySun) WIKI("Data relating to the sun") )
	SkyTimeSecondarySun **secSunValues;				AST( NAME(SkySecondarySun) WIKI("Data relating to the secondary sun") )
	SkyTimeCharacterLighting **charLightingValues;	AST( NAME(SkyCharacterLighting) WIKI("Data relating to lighting of characters") )
	SkyTimeCloudShadows **cloudShadowValues;		AST( NAME(SkyCloudShadows) WIKI("Data relating to the cloud shadows") )
	SkyTimeShadowFade **shadowFadeValues;			AST( NAME(SkyShadowFade) WIKI("Data relating to shadow fading") )
	SkyTimeColorCorrection **colorCorrectionValues;	AST( NAME(SkyColorCorrection) WIKI("Data relating to postprocess color correction") )
	SkyTimeTint **tintValues;						AST( NAME(SkyTint) WIKI("Data relating to the tint") )
	SkyTimeOutline **outlineValues;					AST( NAME(SkyOutline) WIKI("Data relating to the outline") )
	SkyTimeFog **fogValues;							AST( NAME(SkyFog) WIKI("Data relating to the fog") )
	SkyTimeLightBehavior **lightBehaviorValues;		AST( NAME(SkyLightBehavior) WIKI("Data relating to the tone mapping") )
	SkyTimeBloom **bloomValues;						AST( NAME(SkyBloom) WIKI("Data relating to the bloom") )
	DOFValues **dofValues;							AST( NAME(DOFValues) STRUCT(parse_DOFValues) WIKI("Depth of Field Values") )
	SkyTimeAmbientOcclusion **occlusionValues;		AST( NAME(AmbientOcclusion) WIKI("Data relating to screen-space ambient occlusion") )
	SkyTimeWind	**windValues;						AST( NAME(Wind) WIKI("Data relating to the wind") )
	SkyTimeScattering **scatteringValues;			AST( NAME(SkyScattering) WIKI("Local scattering parameters at various times.") )
	SkyDome **skyDomes;								AST( NAME(SkyDome) WIKI("Defines a sky dome") )
	ShadowRules **shadowValues;						AST( NAME(SkyShadows) WIKI("Parallel Split Shadow Map rules to override World Region rules (optional)") )

	U32		bfParamsSpecified[2];					AST( USEDFIELD )
	U32		bfParamsSize;							NO_AST

} SkyInfo;
extern ParseTable parse_SkyInfo[];
#define TYPE_parse_SkyInfo SkyInfo

AUTO_STRUCT WIKI("Sky File After Blending");
typedef struct BlendedSkyInfo
{
	U32 specified_values;

	bool	ignoreFogClipFar;						AST( NAME(IgnoreFogClip) )
	bool	fogClipFar;								AST( NAME(FogClip) )
	bool	ignoreFogClipLow;						AST( NAME(IgnoreFogClipLow) )
	bool	fogClipLow;								AST( NAME(FogClipLow) )

	const char* cloudShadowTexture;					AST( NAME(CloudShadowTexture) POOL_STRING )
	const char* diffuseWarpTextureCharacter;		AST( NAME(DiffuseWarpTextureCharacter) POOL_STRING )
	const char* diffuseWarpTextureWorld;			AST( NAME(DiffuseWarpTextureWorld) POOL_STRING )
	const char* ambientCube;						AST( NAME(ambientCube) POOL_STRING )
	const char* reflectionCube;						AST( NAME(reflectionCube) POOL_STRING )

	SkyTimeSun sunValues;							AST( NAME(SkySun) )
	SkyTimeSecondarySun secSunValues;				AST( NAME(SkySecondarySun) )
	SkyTimeCharacterLighting charLightingValues;	AST( NAME(SkyCharacterLighting) )
	SkyTimeCloudShadows cloudShadowValues;			AST( NAME(SkyCloudShadows) )
	SkyTimeShadowFade shadowFadeValues;				AST( NAME(SkyShadowFade) )
	SkyTimeColorCorrection colorCorrectionValues;	AST( NAME(SkyColorCorrection) )
	SkyTimeTint tintValues;							AST( NAME(SkyTint) )
	SkyTimeOutline outlineValues;					AST( NAME(SkyOutline) )
	SkyTimeFog fogValues;							AST( NAME(SkyFog) )
	SkyTimeLightBehavior lightBehaviorValues;		AST( NAME(SkyLightBehavior) )
	SkyTimeBloom bloomValues;						AST( NAME(SkyBloom) )
	DOFValues dofValues;							AST( NAME(DOFValues) STRUCT(parse_DOFValues) )
	SkyTimeAmbientOcclusion occlusionValues;		AST( NAME(AmbientOcclusion) )
	SkyTimeWind windValues;							AST( NAME(Wind) )
	SkyTimeScattering scatteringValues;				AST( NAME(SkyScattering) )
	ShadowRules shadowValues;						AST( NAME(SkyShadows) )

	BlendedSkyDome *skyDomes[MAX_SKY_DOMES];		NO_AST
	int		skyDomeCount;							NO_AST

	Vec3	outputWorldLightDir;
	Vec3	outputWorldLightDir2;
	LightAffectType secondaryLightType2;

}BlendedSkyInfo;
extern ParseTable parse_BlendedSkyInfo[];
#define TYPE_parse_BlendedSkyInfo BlendedSkyInfo

typedef struct GfxSkyData
{
	F32 time;
	int sky_needs_reload;							//Used to tell if the visible_sky needs to be cleared

	SkyInfoGroupInstantiated **active_sky_groups;	//List of Sky Groups that are being drawn
	BlendedSkyInfo *visible_sky;					//Sky that is visible on screen after blending and everything

	//Custom DOF Override
	char *custom_dof_sky_name;						//Name of a generated sky to be used for the DOF Override
	SkyInfoGroup *custom_dof_sky_group;				//Sky Group used for Override
	bool dof_needs_reload;							//Used to cause DOF values to clear

	//Custom Fog Override
	char *custom_fog_sky_name;						//Name of a generated sky to be used for the Fog Override
	SkyInfoGroup *custom_fog_sky_group;				//Sky Group used for Override
	bool fog_needs_reload;							//Use to cause fog values to clear
} GfxSkyData;

extern ParseTable parse_SkyDome[];
#define TYPE_parse_SkyDome SkyDome

typedef void (*gfxSkyOpOnSkyFunc)(ParseTable pti[], U32 flag, SkyInfo *sky, BlendedSkyInfo *blend_sky, void **sky_values, void *blended_sky_values, void *user_data);
typedef void (*gfxSkyOpOnBlendedSkyFunc)(ParseTable pti[], U32 flag, BlendedSkyInfo *sky_1, BlendedSkyInfo *sky_2, void *values_1, void *values_2, void *user_data);

void gfxSkyLoadSkies();
void gfxSkyValidateSkies(void);
void gfxSkyReloadAllSkies(void);
void gfxSkyClearAllVisibleSkies(void);
void gfxSkyClearVisibleSky(GfxSkyData *sky_data);

GfxSkyData *gfxSkyCreateSkyData();
void gfxSkyDestroySkyData(GfxSkyData *sky_data);

const BlendedSkyInfo* gfxSkyGetVisibleSky(const GfxSkyData *sky_data);
void gfxSkyQueueDrawables(GfxCameraView *camera_view);

void gfxSkyUpdate(GfxCameraView *camera_view, float time);
void gfxSkyDraw(GfxSkyData *sky_data, bool hdr_pass);
void gfxSkyFillDebugText(GfxSkyData *sky_data);
void gfxSkySetFillingDebugTextFlag(bool val);
char *gfxSkyGetDebugText();

void gfxSkyGroupUpdatePositionalFade(GfxSkyData *sky_data,SkyInfoGroup* sky_group,F32 fPercent);
void gfxSkyGroupFadeTo(GfxSkyData *sky_data, SkyInfoGroup* sky_group, F32 start_percent, F32 desired_percent, F32 fade_rate, U32 priority, bool delete_on_fade);
F32 gfxSkyRemoveSkyGroup(GfxSkyData *sky_data, SkyInfoGroup* sky_group);
void gfxSkyClearActiveSkyGroups(GfxSkyData *sky_data);

bool gfxSkyCheckSkyExists(const char *sky_filename);

//WARNING: Keep in mind that the pointer returned is from a dictionary and you should not hold onto this pointer for too long
SkyInfo *gfxSkyFindSky(const char *sky_filename);

void gfxSkyFindValuesSurroundingTime(const void **sky_data, int array_size, F32 curTime, int *sky_early_idx, int *sky_late_idx, F32 *ratio);
void gfxSkyFixupSkyDomes(SkyInfo *sky_info);
void gfxSkyReloadSkiesCB(const char* relpath, int when);

bool gfxSkyGroupIsIndoor(SkyInfoGroup *sky_group);

void gfxSkyDrawLensFlare(LensFlarePiece **flare_pieces, U32 num_flares, const Frustum *camera_frustum,
	const Vec3 light_pos_ws, const Vec3 tint_color, float alpha_fade, int lens_flare_num);

AUTO_STRUCT;
typedef struct OverrideTable {
	char *name;				AST( NAME(name) WIKI("name of the field") )
	S32 overrideValue;		AST( NAME(overrideValue) WIKI("value of the field") )
} OverrideTable;
extern ParseTable parse_OverrideTable[];
#define TYPE_parse_OverrideTable OverrideTable

// The whole purpose of this struct is just to be a holder of a string to be used for a UIList (or whatever else demands a ParseTable).
AUTO_STRUCT;
typedef struct charTable {
	char *name;				AST( NAME(name) WIKI("name of the field") )
} charTable;
extern ParseTable parse_charTable[];
#define TYPE_parse_charTable charTable

