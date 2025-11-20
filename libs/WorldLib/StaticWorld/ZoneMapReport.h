/***************************************************************************



***************************************************************************/

#ifndef _ZONEMAPREPORT_H_
#define _ZONEMAPREPORT_H_
GCC_SYSTEM

#include "GlobalEnums.h"
#include "WorldLibEnums.h"
#include "ResourceManager.h"

typedef struct ZoneMapInfo ZoneMapInfo;

///////////////////////////////////////////////////
//		ZoneMapInfo functions
///////////////////////////////////////////////////

//		Resource dictionary

void							zmapInfoReport(FILE * report_file);

#endif //_ZONEMAPREPORT_H_

