#include "GlobalTypes.h"
#include "gslentity.h"
#include "error.h"
#include "errornet.h"
#include "ticketnet.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "textparser.h"
#include "estring.h"
#include "utilitiesLib.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "ServerLib.h"
#include "entity.h"
#include "worldgrid.h"
#include "RegionRules.h"
#include "allegiance.h"
#include "Character.h"

#include "inventoryCommon.h"
#include "CharacterAttribs.h"

#include "Autogen/entity_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"



char *OVERRIDE_LATELINK_entity_CreateProjSpecificLogString(Entity *entity)
{
	static const char *s_pcRegionString = NULL;

	PERFINFO_AUTO_START_FUNC();
	{
		int Level = inv_GetNumericItemValue(entity, "Level");
		AllegianceDef *Allegiance = GET_REF(entity->hAllegiance);
		const char *pcAllegianceString;

		if (!s_pcRegionString) {
			RegionRules* pRules = getRegionRulesFromZoneMap(NULL);
			if (pRules) {
				switch(pRules->eRegionType) {
					xcase WRT_Ground:		s_pcRegionString = "GND";
					xcase WRT_Space:		s_pcRegionString = "SYS";
					xcase WRT_SystemMap:	s_pcRegionString = "SYS";
					xcase WRT_SectorSpace:	s_pcRegionString = "SEC";
				}
			} 
			if (!s_pcRegionString) {
				s_pcRegionString = "DEF";
			}
		}

		if (!entity->estrProjSpecificLogString)
			estrCreate(&entity->estrProjSpecificLogString);

		if(Allegiance)
		{
			pcAllegianceString = Allegiance->pcLogName;
		}
		else
		{
			pcAllegianceString = "NN";
		}

		estrPrintf(&entity->estrProjSpecificLogString, "LEV %d,REG %s,ALL %s", Level, s_pcRegionString,pcAllegianceString);

		if (entGetVirtualShardID(entity))
		{
			estrConcatf(&entity->estrProjSpecificLogString, ",VSH %d", entGetVirtualShardID(entity));
		}
	}
	PERFINFO_AUTO_STOP();

	return entity->estrProjSpecificLogString;
}