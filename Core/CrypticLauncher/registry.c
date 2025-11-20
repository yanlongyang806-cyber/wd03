#include "registry.h"
#include "error.h"
#include "RegistryReader.h"
#include "GlobalTypes.h"
#include "resource_CrypticLauncher.h"
#include "launcherUtils.h"
#include "GameDetails.h"

#include "wininclude.h"
#include "estring.h"
#include "earray.h"

int readRegStr(const char *prodName, const char *name, char *outbuffer, int outlen, char **history)
{
	RegReader reader;
	const char *dispName;
	int ret;

	if(!prodName)
		prodName = GetProductName();

	dispName = gdGetDisplayName(gdGetIDByName(prodName));

	reader = createRegReader();
	if(!initRegReaderEx(reader, CRYPTIC_REG_KEY, dispName))
		return 0;
	ret = rrReadString(reader, name, outbuffer, outlen);
	if(!ret && history)
	{
		char histProdName[64], *p;
		FOR_EACH_IN_EARRAY(history, char, h)
			if(!h) continue;
			strcpy(histProdName, h);
			p = strchr(histProdName, ':');
			if(p) *p = '\0';
			if(!histProdName[0]) continue;
			ret = readRegStr(histProdName, name, outbuffer, outlen, NULL);
			if(ret)
				return ret;
		FOR_EACH_END
			
	}
	destroyRegReader(reader);
	return ret;
}

int writeRegStr(const char *prodName, const char *name, const char *str)
{
	RegReader reader;
	const char *dispName;
	int ret;

	if(!prodName)
		prodName = GetProductName();

	dispName = gdGetDisplayName(gdGetIDByName(prodName));

	reader = createRegReader();
	initRegReaderEx(reader, CRYPTIC_REG_KEY, dispName);
	ret = rrWriteString(reader, name, str);
	destroyRegReader(reader);
	return ret;
}

int readRegInt(const char *prodName, const char *name, unsigned int *out, char **history)
{
	RegReader reader;
	const char *dispName;
	int ret;

	if(!prodName)
		prodName = GetProductName();

	dispName = gdGetDisplayName(gdGetIDByName(prodName));

	reader = createRegReader();
	if(!initRegReaderEx(reader, CRYPTIC_REG_KEY, dispName))
		return 0;
	ret = rrReadInt(reader, name, out);
	if(!ret && history)
	{
		char histProdName[64], *p;
		FOR_EACH_IN_EARRAY(history, char, h)
			if(!h) continue;
		strcpy(histProdName, h);
		p = strchr(histProdName, ':');
		if(p) *p = '\0';
		if(!histProdName[0]) continue;
		ret = readRegInt(histProdName, name, out, NULL);
		if(ret)
			return ret;
		FOR_EACH_END

	}
	destroyRegReader(reader);
	return ret;
}

int writeRegInt(const char *prodName, const char *name, unsigned int val)
{
	RegReader reader;
	const char *dispName;
	int ret;

	if(!prodName)
		prodName = GetProductName();

	dispName = gdGetDisplayName(gdGetIDByName(prodName));

	reader = createRegReader();
	if(!initRegReaderEx(reader, CRYPTIC_REG_KEY, dispName))
		return 0;
	ret = rrWriteInt(reader, name, val);
	destroyRegReader(reader);
	return ret;
}

