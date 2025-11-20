// HACK: Also included in AssetManager.  Make this something in UtilitiesLib (but don't want on the server!)

#include "AppLocale.h"
#include "SoundLib.h"

// Command line option to set the client's locale
AUTO_COMMAND ACMD_NAME("Locale") ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setClientLocale(const char *pcLocale)
{
	int loc;	
	const char *pcActualLocale;

	loc = locGetIDByName(pcLocale);
	setCurrentLocale(loc);

	pcActualLocale = locGetName(loc);
	if (stricmp(pcActualLocale, pcLocale) != 0) {
		printf("Client starting in locale %s instead of non-existent request for locale %s\n", pcActualLocale, pcLocale);
	} else {
		printf("Client starting in locale %s\n", pcActualLocale);
	}

	sndSetLanguage(pcActualLocale);
}