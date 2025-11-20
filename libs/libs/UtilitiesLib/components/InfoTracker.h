#pragma once
GCC_SYSTEM

#define INFO_TRACKING_ON 1

#if INFO_TRACKING_ON
	#define infotrackStartFrame(infotag, subtype) infotrackStartFrameInternal(infotag, subtype)
	#define infotrackEndFrame(infotag) infotrackEndFrameInternal(infotag)
	#define infotrackIncrementCountf(infotag, formatStr, ...) infotrackIncrementCountfInternal(infotag, FORMAT_STRING_CHECKED(formatStr), __VA_ARGS__)
#else
	#define infotrackStartFrame(infotag, subtype)
	#define infotrackEndFrame(infotag)
	#define infotrackIncrementCountf(infotag, formatStr, ...)
#endif

void infotrackStartFrameInternal(SA_PARAM_NN_STR const char* infotag, SA_PARAM_OP_STR const char* subtype);
void infotrackEndFrameInternal(SA_PARAM_NN_STR const char* infotag);
void infotrackIncrementCountfInternal(SA_PARAM_NN_STR const char* infotag, FORMAT_STR const char* formatStr, ...);

void infotrackPrint(SA_PARAM_NN_STR const char* infotag);
