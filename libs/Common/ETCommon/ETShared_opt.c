#include "ETCommon/ETShared.h"
#include "earray.h"
#include "file.h"


// Returns true if any of the executables were run in some TOOLS/BIN subdir
bool RunningFromToolsBin(ErrorEntry *p)
{
	int i;
	char tempFileName[MAX_PATH];
	for (i=0; i < eaSize(&p->ppExecutableNames); i++)
	{
		if (p->ppExecutableNames[i])
		{
			fileFixUpName(p->ppExecutableNames[i], tempFileName);
			if (strstr(tempFileName, "/TOOLS/BIN"))
				return true;
		}
	}
	return false;
}
