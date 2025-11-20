#include "wlState.h"
#include "cmdparse.h"
#include "wlModelData.h"
#include "dynFxManager.h"
#include "utils.h"

WorldLibState wl_state={0};

// Unloads all model geometry
AUTO_COMMAND;
void ModelUnload(void)
{
	modelFreeAllCache(0);
}

// Set Animation playback rate
AUTO_CMD_FLOAT(dynDebugState.fAnimRate, setAnimRate);

// Set DynFx playback rate
AUTO_CMD_FLOAT(dynDebugState.fDynFxRate, setDynFxRate);

// Turn off world animations
AUTO_CMD_INT(wl_state.debug.disable_world_animation, disableWorldAnimation) ACMD_CATEGORY(Debug);

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME(bcnNoLoad, noAI) ACMD_HIDE ACMD_CMDLINE;
void bcnNoLoad(int value)
{
	wl_state.dont_load_beacons = value;
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME(bcnNoLoadIfSpace) ACMD_HIDE ACMD_CMDLINE;
void bcnNoLoadIfSpace(int value)
{
	wl_state.dont_load_beacons_if_space = value;
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_HIDE ACMD_CMDLINE;
void doNotTakePhotos(int value)
{
	wl_state.dont_take_photos = value;
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_HIDE ACMD_CMDLINE;
void MapSnapIterations(int value)
{
	wl_state.photo_iterations = value;
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void MaterialEnableFeature(CmdContext *cmd_context, ACMD_NAMELIST(ShaderGraphFeaturesEnum, STATICDEFINE) const char* featureName)
{
	ShaderGraphFeatures feature = StaticDefineIntGetInt( ShaderGraphFeaturesEnum, featureName );
	
	system_specs.material_supported_features |= feature;
	globCmdParse( "reloadMaterials" );
	systemSpecsUpdateString();
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void MaterialDisableFeature(CmdContext *cmd_context, ACMD_NAMELIST(ShaderGraphFeaturesEnum, STATICDEFINE) const char* featureName)
{
	ShaderGraphFeatures feature = StaticDefineIntGetInt( ShaderGraphFeaturesEnum, featureName );
	
	system_specs.material_supported_features &= ~feature;
	globCmdParse( "reloadMaterials" );
	systemSpecsUpdateString();
}

// Bins all geometry files
AUTO_CMD_INT(wl_state.binAllGeos, binAllGeos) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Checks all geo bins for certain corrupt values
AUTO_CMD_INT(wl_state.verifyAllGeoBins, verifyAllGeoBins) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// When binning, look for corrupt LODs
AUTO_CMD_INT(wl_state.checkForCorruptLODs, checkForCorruptLODs) ACMD_CMDLINE ACMD_CATEGORY(Debug);

AUTO_COMMAND ACMD_CATEGORY(Debug);
void MaterialSetFeatures(const ACMD_SENTENCE featuresRawString)
{
	char* strtokContext = NULL;
	char* featuresString;
	char* featureName;

	strdup_alloca( featuresString, featuresRawString );
	featureName = strtok_s( featuresString, " \t\n\r", &strtokContext );

	if( featureName ) {
		system_specs.material_supported_features = 0;
		while( featureName ) {
			ShaderGraphFeatures feature = StaticDefineIntGetInt( ShaderGraphFeaturesEnum, featureName );
			system_specs.material_supported_features |= feature;

			featureName = strtok_s( NULL, " \t\n\r", &strtokContext );
		}

		globCmdParse( "reloadMaterials" );
		systemSpecsUpdateString();
	}
}

static const struct {
	const char* featureName;
	ShaderGraphFeatures feature;
} relevantFeaturesList[] = {
	"SM3+ - HIGH END (GeForce 8800, ATI HD 3800)",
	SGFEAT_SM30_PLUS | SGFEAT_SM30 | SGFEAT_SM20_PLUS | SGFEAT_SM20,
	"SM3 - MIDRANGE (ATI X1950)",
	SGFEAT_SM30 | SGFEAT_SM20_PLUS | SGFEAT_SM20,
	"SM2+ - LOW MIDRANGE (ATI X850)",
	SGFEAT_SM20_PLUS | SGFEAT_SM20,
	"SM2 - LOW END (ATI 9800, GeForce FX)",
	SGFEAT_SM20,
	"FIXED FUNCTION - SUPER LOW END (GeForce 4)",
	0,
	"SM3* - SLOW HIGH END",
	SGFEAT_SM30_HYPER | SGFEAT_SM30_PLUS | SGFEAT_SM30 | SGFEAT_SM20_PLUS | SGFEAT_SM20,
};

AUTO_COMMAND ACMD_CATEGORY(Debug);
void MaterialToggleFeatures(void)
{
	int it = 0;

	while( (relevantFeaturesList[ it ].feature & system_specs.material_supported_features)
		   != relevantFeaturesList[ it ].feature ) {
		++it;
	}

	it = (it + 1) % ARRAY_SIZE( relevantFeaturesList );
	system_specs.material_supported_features = relevantFeaturesList[ it ].feature;
	globCmdParse( "reloadMaterials" );
	wlStatusPrintf( "Now displaying as %s card.", relevantFeaturesList[ it ].featureName );
	systemSpecsUpdateString();
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void MaterialToggleFeaturesSimple(void)
{
	int it;
	
	if( (system_specs.material_supported_features & SGFEAT_ALL_DEDUCED) != SGFEAT_ALL_DEDUCED ) {
		it = 0;
	} else {
		it = ARRAY_SIZE( relevantFeaturesList ) - 3;
	}
	system_specs.material_supported_features = relevantFeaturesList[ it ].feature;
	globCmdParse( "reloadMaterials" );
	wlStatusPrintf( "Now displaying as %s card.", relevantFeaturesList[ it ].featureName );
	systemSpecsUpdateString();
}

AUTO_COMMAND ACMD_CMDLINE;
void controlfpTest(int enable)
{
	int ignored;

	if( enable ) {
		_controlfp_s( &ignored, _EM_INEXACT | _EM_UNDERFLOW | _DN_FLUSH, _MCW_DN | _MCW_EM );
	} else {
		_controlfp_s( &ignored, _CW_DEFAULT, 0xFFFFF );
	}
}

// Makes all private maps visible (dev mode only)
AUTO_CMD_INT(wl_state.allow_all_private_maps, allowAllPrivateMaps) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Makes all maps private to groups (e.g. Design) visible
AUTO_COMMAND ACMD_NAME(allowGroupPrivateMaps) ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void wlAllowGroupPrivateMaps(int d)
{
	wl_state.allow_group_private_maps = !!d;
}

// Makes all materials become an unlit, solid black
AUTO_CMD_INT(wl_state.matte_materials, matte_materials) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Specify a desired material score
AUTO_COMMAND  ACMD_CATEGORY(Debug);
void wlSetDesiredMaterialScore(int desired_score)
{
	if (wl_state.desired_quality != desired_score)
	{
		wl_state.desired_quality = desired_score;
		if (wl_state.gfx_material_reload_all)
			wl_state.gfx_material_reload_all();
	}
}