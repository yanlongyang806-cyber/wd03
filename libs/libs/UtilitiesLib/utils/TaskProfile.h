#ifndef _TASKPROFILE_H_
#define _TASKPROFILE_H_

#pragma once

AUTO_STRUCT;
typedef struct TaskProfileTime
{
	S64 totalTimeAccum;
	S64 startTimeMarker;
} TaskProfileTime;

AUTO_STRUCT;
typedef struct TaskProfile
{
	TaskProfileTime totalTaskTime;
	S64 bytesRead;
	S64 bytesWrite;
} TaskProfile;

extern ParseTable parse_TaskProfile[];
#define TYPE_parse_TaskProfile TaskProfile

S64 GetCPUTicks64();
F32 GetCPUTicksMsScale();

__forceinline void taskStartTimer(TaskProfile * timedTask)
{
	timedTask->totalTaskTime.startTimeMarker = GetCPUTicks64();
}

__forceinline void taskStopTimer(TaskProfile * timedTask)
{
	timedTask->totalTaskTime.totalTimeAccum += GetCPUTicks64() - timedTask->totalTaskTime.startTimeMarker;
	timedTask->totalTaskTime.startTimeMarker = 0;
}

__forceinline void taskAttributeWriteIO(TaskProfile * measuredTask, S64 bytesWritten)
{
	measuredTask->bytesWrite += bytesWritten;
}

__forceinline void taskAttributeReadIO(TaskProfile * measuredTask, S64 bytesRead)
{
	measuredTask->bytesRead += bytesRead;
}

__forceinline void taskAttributeReadWriteIO(TaskProfile * measuredTask, S64 bytesReadWrite)
{
	measuredTask->bytesWrite += bytesReadWrite;
	measuredTask->bytesRead += bytesReadWrite;
}

#endif //_TASKPROFILE_H_
