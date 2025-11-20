/***************************************************************************
 *     Copyright (c) 2013-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#include "stdtypes.h"
#include "Entity.h"
#include "Player.h"
#include "Character.h"
#include "EntitySavedData.h"
#include "../StaticWorld/ZoneMap.h"
#include "GameStringFormat.h"
#include "StringCache.h"


#include "Gateway/gslGatewaySession.h"
//#include "EntityLib.h"
//#include "Gateway/gslGatewayMappedEntity.h"

#include "gslGatewayMappedLoginEntity_c_ast.h"


AUTO_STRUCT;
typedef struct MappedLoginEntity
{
	ContainerID id;

	char *estrName;					AST(ESTRING NAME(Name))
	char *estrClassName;			AST(ESTRING NAME(ClassName))
	char *estrLastMapName;			AST(ESTRING NAME(LastMapName))
	char *estrGenderName;			AST(ESTRING NAME(GenderName))
	char *estrSpeciesName;			AST(ESTRING NAME(SpeciesName))

	const char *pcClassType;		AST(POOL_STRING NAME(ClassType))
	char *estrPublicAccountName;	AST(ESTRING NAME(PublicAccountName))
	char *estrGuildName;			AST(ESTRING NAME(GuildName))

	char *estrLastPlayed;			AST(ESTRING NAME(LastPlayed))
		// Must be an RFC822 Date
} MappedLoginEntity;


/////////////////////////////////////////////////////////////////////////////

//
// GetLastMapName
//
// Does the dance necessary to get the most recently visited map's name.
//
static const char *GetLastMapName(Language lang, Entity *psrc)
{
	if(psrc->pSaved)
	{
		ZoneMapInfo *pzmi = NULL;
		Message *pmsg;

		if(psrc->pSaved->lastStaticMap)
		{
			pzmi = zmapInfoGetByPublicName(psrc->pSaved->lastStaticMap->mapDescription);
		}
		else if(psrc->pSaved->lastNonStaticMap)
		{
			pzmi = zmapInfoGetByPublicName(psrc->pSaved->lastNonStaticMap->mapDescription);
		}

		if(pzmi)
		{
			pmsg = zmapInfoGetDisplayNameMessagePtr(pzmi);
			if(pmsg)
			{
				return langTranslateMessage(lang, pmsg);
			}
		}
	}

	return NULL;
}

static void SetClassNameType(Entity *pEntity, const char ** pcClassTypeName)
{
	if (pEntity && pEntity->pChar)
	{
		CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
		if(pClass && pClass->pchName)
		{
			*pcClassTypeName = allocAddString(pClass->pchName);
		}
	}

	if(!(*pcClassTypeName))
	{
		*pcClassTypeName = allocAddString("None");
	}
}

MappedLoginEntity *CreateMappedLoginEntity(GatewaySession *psess, ContainerTracker *ptracker, MappedLoginEnity *pv)
{
	MappedLoginEntity *pdest;
	Language lang = psess->lang;
	CharacterClass *pClass = NULL;

	Entity *psrc = GET_REF(ptracker->hEntity);
	
	if(!psrc)
		return NULL;

	pdest = StructAlloc(parse_MappedLoginEntity);

	pdest->id = psrc->myContainerID;
	
	if(psrc->pPlayer && psrc->pPlayer->publicAccountName)
	{
		estrPrintf(&pdest->estrPublicAccountName, "%s", psrc->pPlayer->publicAccountName);
	}

	langFormatGameString(lang, &pdest->estrName, "{Entity.Name}", STRFMT_ENTITY(psrc), STRFMT_END);
	langFormatGameString(lang, &pdest->estrClassName, "{Entity.Class}", STRFMT_ENTITY(psrc), STRFMT_END);
	langFormatGameString(lang, &pdest->estrGenderName, "{Entity.SpeciesGender}", STRFMT_ENTITY(psrc), STRFMT_END);
	langFormatGameString(lang, &pdest->estrSpeciesName, "{Entity.Species}", STRFMT_ENTITY(psrc), STRFMT_END);
	
	estrCopy2(&pdest->estrLastMapName, GetLastMapName(lang, psrc));
	SetClassNameType(psrc, &pdest->pcClassType);

	// If there is a guild ID this ent is in a guild. 
	// For some reason the normal guild_IsMember check doesn't work here
	if (SAFE_MEMBER2(psrc, pPlayer, iGuildID))
	{
		estrCopy2(&pdest->estrGuildName, psrc->pPlayer->pcGuildName);
	}

	if(psrc->pPlayer)
	{
		estrCopy2(&pdest->estrLastPlayed, timeGetRFC822StringFromSecondsSince2000(psrc->pPlayer->iLastPlayedTime));
	}

	return pdest;
}

/////////////////////////////////////////////////////////////////////////////

void DestroyMappedLoginEntity(GatewaySession *psess, ContainerTracker *ptracker, MappedLoginEntity *pent)
{
	if(pent)
	{
		StructDestroy(parse_MappedLoginEntity, pent);
	}
}

#include "gslGatewayMappedLoginEntity_c_ast.c"

// End of File
