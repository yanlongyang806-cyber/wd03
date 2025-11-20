#include "HUDOptionsCommon.h"
#include "ControlScheme.h"
#include "error.h"
#include "Player.h"
#include "Player_h_ast.h"

PlayerHUDOptionsStruct g_DefaultHUDOptions = {0};

static U32 s_ShowOverheadOffsets[] =
{
	offsetof(PlayerShowOverhead, eShowEnemy),
	offsetof(PlayerShowOverhead, eShowFriendlyNPC),
	offsetof(PlayerShowOverhead, eShowFriends),
	offsetof(PlayerShowOverhead, eShowSupergroup),
	offsetof(PlayerShowOverhead, eShowTeam),
	offsetof(PlayerShowOverhead, eShowPet),
	offsetof(PlayerShowOverhead, eShowPlayer),
	offsetof(PlayerShowOverhead, eShowEnemyPlayer),
	offsetof(PlayerShowOverhead, eShowSelf),
};
STATIC_ASSERT(ARRAY_SIZE(s_ShowOverheadOffsets) == OVERHEAD_ENTITY_TYPE_COUNT);

bool upgradeHUDOptions(PlayerHUDOptions* pSrcOptions, PlayerHUDOptions* pDstOptions)
{
	PlayerHUDOptions* pDefaults = NULL;
	bool bChanged = false;
	int i;

	if (pSrcOptions->iVersion < 1)
	{

		if (!pDefaults)
			pDefaults = getDefaultHUDOptions(pSrcOptions->eRegion);
		if (!bChanged)
		{
			StructCopyAll(parse_PlayerHUDOptions, pSrcOptions, pDstOptions);
			pSrcOptions = pDstOptions;
		}

		// Copy power mode flags from defaults
		for (i = 0; i < OVERHEAD_ENTITY_TYPE_COUNT; i++)
		{
			OverHeadEntityFlags *srcFlags = (OverHeadEntityFlags *)((char*)&pDefaults->ShowOverhead + s_ShowOverheadOffsets[i]);
			OverHeadEntityFlags *dstFlags = (OverHeadEntityFlags *)((char*)&pDstOptions->ShowOverhead + s_ShowOverheadOffsets[i]);
			*dstFlags = (*dstFlags & ~OVERHEAD_ENTITY_FLAG_POWERMODEALL) | (*srcFlags & OVERHEAD_ENTITY_FLAG_POWERMODEALL);
		}

		pDstOptions->iVersion = 1;
		bChanged = true;
	}

	return bChanged;
}

PlayerHUDOptions* getDefaultHUDOptions(S32 eSchemeRegion)
{
	S32 i;
	static bool bInitDefault = false;
	static PlayerHUDOptions s_Default = {0};
	for (i = eaSize(&g_DefaultHUDOptions.eaHUDOptions)-1; i >= 0; i--)
	{
		PlayerHUDOptions* pHUDOptions = g_DefaultHUDOptions.eaHUDOptions[i];

		if (pHUDOptions->eRegion == eSchemeRegion)
		{
			return pHUDOptions;
		}
	}
	if (eaSize(&g_DefaultHUDOptions.eaHUDOptions) > 0)
		Errorf("HUDOptionsDefaults.def exists, but no default specified for current regiontype (%s).", StaticDefineIntRevLookup(ControlSchemeRegionTypeEnum, eSchemeRegion));
	if (!bInitDefault)
	{
		StructInit(parse_PlayerHUDOptions, &s_Default);
		s_Default.iVersion = HUDOPTIONS_VERSION;
		bInitDefault = true;
	}
	s_Default.eRegion = eSchemeRegion;
	return &s_Default;
}

AUTO_STARTUP(HUDOptionsDefaults) ASTRT_DEPS(AS_ControlSchemeRegions);
void HudOptions_LoadDefaults(void)
{
	S32 i;

	loadstart_printf("Loading HUDOptionsDefaults...");

	StructInit(parse_PlayerHUDOptionsStruct, &g_DefaultHUDOptions);

	ParserLoadFiles(NULL, "defs/config/HUDOptionsDefaults.def", "HUDOptionsDefaults.bin", PARSER_OPTIONALFLAG, parse_PlayerHUDOptionsStruct, &g_DefaultHUDOptions);

	// Set hud options to current version number
	for (i = eaSize(&g_DefaultHUDOptions.eaHUDOptions) - 1; i >= 0; i--)
		g_DefaultHUDOptions.eaHUDOptions[i]->iVersion = HUDOPTIONS_VERSION;

	loadend_printf(" done (HUDOptionsDefaults).");
}
