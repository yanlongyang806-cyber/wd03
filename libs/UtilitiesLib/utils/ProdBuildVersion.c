//this is a fairly magical file. When it looks like it does here, it has magic numbers so that ProdVersionStamper can 
//search through the file, find the prod version string, and overwrite it.
//
//For "real" production builds (baselines/incrs), this entire file gets replaced by an auto-generated one containing
//just a literal string with the version in it, ie, 
//char prodVersion[256] = "SVN 235.7(blah blah)"

//NOTE if you change anything in this file change ProdVersionStamper.c

typedef struct prodBuildVersionStruct
{
	U32 iMagic[4];
	char versionName[256];
} prodBuildVersionStruct;

prodBuildVersionStruct prodBuildVersion = 
{
	{
	0x13579BDF,
	0xFDB97531,
	0x2468ACE0,
	0x0ECA8642,
	},
	"none"
};

char prodVersion[256] = "";

U32 *GetProdVersionMagicNumbers(void)
{
	return prodBuildVersion.iMagic;
}

AUTO_RUN_FIRST;
void SetProdVersionInternal(void)
{
	if (!*prodVersion)
		strcpy(prodVersion, prodBuildVersion.versionName);
}
