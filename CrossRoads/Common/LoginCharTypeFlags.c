#include "LoginCommon.h"
#include "GlobalTypes.h"
#include "objPath.h"
#include "error.h"
#include "ResourceInfo.h"
#include "entCritter.h"
#include "AutoGen/LoginCommon_h_ast.h"

S32 g_iNumOfUnlockedAllegianceFlags = 0;
S32 g_iNumOfUnlockedCreateFlags = 0;

AUTO_STARTUP(UnlockedAllegianceFlags) ASTRT_DEPS(Allegiance);
void UnlockedAllegianceFlagsRegDictionary(void)
{
	UnlockableAllegianceNames unlockflags = {0};
	S32 i;

	g_pUnlockedAllegianceFlags = DefineCreate();

	loadstart_printf("Loading UnlockedAllegianceFlags... ");

	ParserLoadFiles(NULL, "defs/config/allegunlock.def", "allegunlock.bin", PARSER_OPTIONALFLAG, parse_UnlockableAllegianceNames, &unlockflags);

	// I use i+1 for the default "uncategorized" index, which is always present
	for (i = 0; i < eaSize(&unlockflags.pchNames); i++)
	{
		DefineAddInt(g_pUnlockedAllegianceFlags, unlockflags.pchNames[i], 1<<i);

		if (IsServer() && g_hAllegianceDict)
		{
			//Verify these match an Allegiance
			int j;
			DictionaryEArrayStruct *pArray = resDictGetEArrayStruct(g_hAllegianceDict);
			const char *temp = unlockflags.pchNames[i];

			if (temp[0] != 'S' || temp[1] != 'T' || temp[2] != '.')
			{
				Errorf("UnlockableAllegianceName %s does not start with \"ST.\"", unlockflags.pchNames[i]);
			}
			else
			{
				temp += 3;
				for (j = eaSize(&pArray->ppReferents)-1; j >= 0; --j)
				{
					AllegianceDef *pAllegiance = eaGet(&pArray->ppReferents, j);
					if (!stricmp(pAllegiance->pcName,temp))
					{
						break;
					}
				}
				if (j < 0)
				{
					Errorf("UnlockableAllegianceName %s does not match an Allegiance", unlockflags.pchNames[i]);
				}
			}
		}
	}
	g_iNumOfUnlockedAllegianceFlags = i+1;

	StructDeInit(parse_UnlockableAllegianceNames, &unlockflags);

	loadend_printf(" done (%d UnlockedAllegianceFlags).", i);
}

AUTO_STARTUP(UnlockedCreateFlags);
void UnlockedCreateFlagsRegDictionary(void)
{
	UnlockableCreateNames unlockflags = {0};
	S32 i;

	g_pUnlockedCreateFlags = DefineCreate();

	loadstart_printf("Loading UnlockedCreateFlags... ");

	ParserLoadFiles(NULL, "defs/config/createunlock.def", "createunlock.bin", PARSER_OPTIONALFLAG, parse_UnlockableCreateNames, &unlockflags);

	// I use i+1 for the default "uncategorized" index, which is always present
	for (i = 0; i < eaSize(&unlockflags.pchNames); i++)
	{
		DefineAddInt(g_pUnlockedCreateFlags, unlockflags.pchNames[i], 1<<i);
	}
	g_iNumOfUnlockedCreateFlags = i+1;

	StructDeInit(parse_UnlockableCreateNames, &unlockflags);

	loadend_printf(" done (%d UnlockedCreateFlags).", i);
}
