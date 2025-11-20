/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CoreCommon.h"
#include "Materials.h"

void OVERRIDE_LATELINK_ProdSpecificGlobalConfigSetup(void)
{
	materialErrorOnMissingFallbacks = false;
}

