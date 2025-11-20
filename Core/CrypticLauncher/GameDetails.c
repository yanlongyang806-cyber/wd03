#include "GameDetails.h"

#include "utils.h"
#include "AppLocale.h"
#include "CrypticLauncher.h"

struct {
	const char *name;
	const char *code;
	const char *displayName;
	const char *launcherURL;
	const char *mainURL;
	const char *qaLauncherURL;
	const char *devLauncherURL;
	const char *pwrdLauncherURL;
	const char *liveShard;
	const char *ptsShard1;
	const char *ptsShard2;
	bool englishOnly;
} GameTable[] = {
	// Do not change the order of these
	{
		"CrypticLauncher",	
		"CL", 
		"Cryptic Launcher", 
		"http://launcher.champions-online.com/", 
		"http://champions-online.com/", 
		"http://qa.fightclub.cryptic.loc/",
		"http://dev-colauncher.crypticstudios.com/",
		PWRD_LAUNCHER_URL,
		NULL,
		NULL,
		NULL,
		false,
	},		// Do not move this entry.  Index 0 is used as the default data.
	{
		"FightClub", 
		"FC", 
		"Champions Online", 
		NULL, 
		NULL,
		NULL,
		NULL,
		NULL,
		"Live",
		NULL,
		"PublicTest",
		true,
	},
	{
		"StarTrek", 
		"ST", 
		"Star Trek Online", 
		"http://launcher.startrekonline.com/", 
		"http://startrekonline.com/", 
		"http://qa.startrek.cryptic.loc/",
		"http://dev-stolauncher.crypticstudios.com/",
		PWRD_LAUNCHER_URL,
		"Holodeck",
		"Tribble",
		"RedShirt",
		false,
	},
	{
		"Creatures", 
		"CN", 
		"Creatures of the Night", 
		NULL, 
		NULL,
		NULL,
		NULL,
		NULL,
		"Nocturne",
		NULL,
		NULL,
		false,
	},
	{
		"Night", 
		"NNO", 
		"Neverwinter", 
		"http://launcher.playneverwinter.com/", 
		"http://www.playneverwinter.com/",
		"http://qa.night.cryptic.loc/",
		"http://dev-nwlauncher.crypticstudios.com/",
		PWRD_LAUNCHER_URL,
		"Neverwinter",
		"NeverwinterTest",
		"NeverwinterAlpha",
		false,
	},
	{
		"PrimalAge", 
		"PA", 
		"Primal Age", 
		NULL, 
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		false,
	},
	{
		"Bronze", 
		"BA", 
		"Bronze Age", 
		NULL, 
		NULL,
		NULL,
		"http://dev-balauncher.crypticstudios.com/",
		NULL,
		"Hades",
		NULL,
		NULL,
		false,
	},
	{
		"Local", 
		"??", 
		"Local", 
		NULL, 
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		false,
	}
};

bool gdIDIsValid(U32 gameID)
{
	if(gameID >= ARRAY_SIZE(GameTable) || gameID < 0)
		return false;
	else
		return true;
}

const char *gdGetName(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	return GameTable[gameID].name;
}

const char *gdGetCode(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	return GameTable[gameID].code;
}

const char *gdGetDisplayName(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	return GameTable[gameID].displayName;
}

const char *gdGetLauncherURL(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].launcherURL)
		return GameTable[gameID].launcherURL;
	else
		return GameTable[DEFAULT_GAME_ID].launcherURL;
}

const char *gdGetURL(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].mainURL)
		return GameTable[gameID].mainURL;
	else
		return GameTable[DEFAULT_GAME_ID].mainURL;
}

const char *gdGetQALauncherURL(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].qaLauncherURL)
		return GameTable[gameID].qaLauncherURL;
	else
		return GameTable[DEFAULT_GAME_ID].qaLauncherURL;
}

const char *gdGetDevLauncherURL(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].devLauncherURL)
		return GameTable[gameID].devLauncherURL;
	else
		return GameTable[DEFAULT_GAME_ID].devLauncherURL;
}

const char *gdGetPWRDLauncherURL(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].pwrdLauncherURL)
		return GameTable[gameID].pwrdLauncherURL;
	else
		return GameTable[DEFAULT_GAME_ID].pwrdLauncherURL;
}

const char *gdGetLiveShard(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].liveShard)
		return GameTable[gameID].liveShard;
	else
		return GameTable[DEFAULT_GAME_ID].liveShard;
}

const char *gdGetPtsShard1(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].ptsShard1)
		return GameTable[gameID].ptsShard1;
	else
		return GameTable[DEFAULT_GAME_ID].ptsShard1;
}

const char *gdGetPtsShard2(U32 gameID)
{
	if(!gdIDIsValid(gameID))
		return NULL;

	if(GameTable[gameID].ptsShard2)
		return GameTable[gameID].ptsShard2;
	else
		return GameTable[DEFAULT_GAME_ID].ptsShard2;
}

U32 gdGetIDByName(const char *name)
{
	U32 i;
	for(i = 0; i < ARRAY_SIZE(GameTable); i++)
	{
		if(stricmp(GameTable[i].name, name)==0)
			return i;
	}

	return DEFAULT_GAME_ID;
}

U32 gdGetIDByCode(const char *code)
{
	U32 i;
	for(i = 0; i < ARRAY_SIZE(GameTable); i++)
	{
		if(stricmp(GameTable[i].code, code)==0)
			return i;
	}

	return DEFAULT_GAME_ID;
}

U32 gdGetIDByExecutable(const char *executablename)
{
	U32 i;

	// Check if either the name or display name are in the executable path.
	for(i = 0; i < ARRAY_SIZE(GameTable); i++)
	{
		if(strstri(executablename, GameTable[i].name))
			return i;
		if(strstri(executablename, GameTable[i].displayName))
			return i;
	}

	// TODO: Word analysis here. <NPK 2009-04-21>

	return DEFAULT_GAME_ID;
}

bool gdIsLocValid(U32 gameID, U32 locID)
{
	if(!gdIDIsValid(gameID))
		return false;
	if(!locIsImplemented(locID))
		return false;
	if(GameTable[gameID].englishOnly)
		return locGetLanguage(locID) == LANGUAGE_ENGLISH;
	return true;
}