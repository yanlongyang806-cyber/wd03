/***************************************************************************



***************************************************************************/

#include "rand.h"
#include "utilitiesLib.h"
#include "timing.h"
#include "fileutil.h"
#include "rgb_hsv.h"
#include "hoglib.h"
#include "qsortG.h"
#include "logging.h"
#include "stringcache.h"
#include "ContinuousBuilderSupport.h"

#include "wlState.h"

#include "WorldGridPrivate.h"
#include "WorldGridLoad.h"
#include "WorldGridLoadPrivate.h"
#include "WorldCellStreaming.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldVariable.h"
#include "ObjectLibrary.h"
#include "wlBeacon.h"
#include "wlAutoLOD.h"
#include "wlVolumes.h"
#include "wlTerrainSource.h"
#include "wlUGC.h"

#include "wininclude.h"

#include "WorldLibEnums_h_ast.h"



void groupFixupChildren(GroupDef *defload)
{
	int i;
	for (i = 0; i < eaSize(&defload->children); i++)
	{
		GroupChild *entry = defload->children[i];
		if (!isNonZeroMat3(entry->mat))
		{
			// Fix up matrix
			createMat3YPR(entry->mat, entry->rot);
			copyVec3(entry->pos, entry->mat[3]);
		}
	
		//ABW removing this assert on the theory that if it was ever going to hit, it would have hit already,
		//and half the time in this function is eaten up by it.
		//assert(isNonZeroMat3(entry->mat));
	}
}

// This function only existed to debug a weird slowdown that Tom had added.
#if 0
void groupAssertChildrenFixed(GroupDef *defload,int iNewChild)
{
	int i;
	for (i = 0; i < eaSize(&defload->children); i++)
	{
		GroupChild *entry = defload->children[i];
		if (i != iNewChild && !isNonZeroMat3(entry->mat))
		{
			devassert(0 && "Take 2: This is important to Robert Marr");

			// Fix up matrix
			createMat3YPR(entry->mat, entry->rot);
			copyVec3(entry->pos, entry->mat[3]);
		}
	}
}
#endif