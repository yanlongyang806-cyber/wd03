/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "ShardCluster.h"

#include "gslGatewayServer.h"


void OVERRIDE_LATELINK_ShardClusterOverviewChanged(void)
{
	Cluster_Overview *pOverview = GetShardClusterOverview_EvenIfNotInCluster();
	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		gslGatewayServer_ShardClusterOverviewChanged(pOverview);
	}
}

// End of File
