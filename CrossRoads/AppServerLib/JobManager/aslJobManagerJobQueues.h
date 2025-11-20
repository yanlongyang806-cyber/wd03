#pragma once
#include "stashTable.h"

typedef struct JobManagerJobGroupState JobManagerJobGroupState;
typedef struct JobManagerJobState JobManagerJobState;

void JobQueues_AddOrStartJob(JobManagerJobGroupState *pGroupState, JobManagerJobState *pJobState);

void InitJobQueues(void);
void UpdateJobQueues(void);


extern StashTable hJobQueuesByName;