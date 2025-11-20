#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef __WORLDCELLBINNING_H__
#define __WORLDCELLBINNING_H__

#define WORLD_CLUSTER_LOG "WorldCluster.log"

typedef struct WorldStreamingPooledInfo WorldStreamingPooledInfo;
typedef struct BinFileList BinFileList;
typedef struct ZoneMap ZoneMap;

SA_RET_OP_VALID BinFileList *worldCellSaveBins(SA_PARAM_NN_STR const char *base_dir, SA_PARAM_NN_STR const char *output_filename, 
											SA_PARAM_NN_STR const char *pooled_output_filename, SA_PARAM_OP_VALID WorldStreamingPooledInfo *old_pooled_data, 
											SA_PARAM_NN_STR const char *header_filename, SA_PARAM_OP_STR const char *bounds_filename);

void setAllowLocalRemesh(int newVal);
void zoneMapClusterDepFileName(ZoneMap *zMap, char* filename, S32 filename_size);

#endif //__WORLDCELLBINNING_H__

