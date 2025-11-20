#include "referencesystem.h"

#include "wlSky.h"

//////////////////////////////////////////////////////////////////////////
// Dummy sky dictionary so the server can send SkyInfo references to the client.
// Also necessary so that the server can write out bin files with references
// to skies in them and have the client be able to read them in correctly.

AUTO_RUN_ANON(memBudgetAddMapping("SkyInfoGroup", BUDGET_World););
AUTO_RUN_ANON(memBudgetAddMapping("SkyInfoOverride", BUDGET_World););

AUTO_STRUCT;
typedef struct WLSkyInfo
{
	int dummy_data;
} WLSkyInfo;

#include "wlSky_h_ast.c"
#include "wlSky_c_ast.c"

static DictionaryHandle wl_sky_dict;

void createServerSkyDictionary(void)
{
	if (!wl_sky_dict)
		wl_sky_dict = RefSystem_RegisterSelfDefiningDictionary("SkyInfo", false, parse_WLSkyInfo, true, false, "Sky");
}

int cmpSkyInfoGroup(const SkyInfoGroup *sky_group1, const SkyInfoGroup *sky_group2)
{
	int i, t;

	t = eaSize(&sky_group2->override_list) - eaSize(&sky_group1->override_list);
	if (t)
		return SIGN(t);

	for (i = 0; i < eaSize(&sky_group1->override_list); ++i)
	{
		const char *sky1, *sky2;
		ANALYSIS_ASSUME(sky_group1->override_list);
		ANALYSIS_ASSUME(sky_group2->override_list);

		sky1 = REF_STRING_FROM_HANDLE(sky_group1->override_list[i]->sky);
		sky2 = REF_STRING_FROM_HANDLE(sky_group2->override_list[i]->sky);

		if (sky1 && !sky2)
			return -1;
		if (sky2 && !sky1)
			return 1;
		if (sky1 && sky2)
		{
			t = stricmp(sky1, sky2);
			if (t)
				return SIGN(t);
		}
	}

	return 0;
}

