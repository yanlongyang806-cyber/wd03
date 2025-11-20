#ifndef ERRORTRACKERHANDLERINFO_H
#define ERRORTRACKERHANDLERINFO_H

AUTO_STRUCT;
typedef struct ErrorTrackerHandlerInfo
{
	int   uID;
	bool  bFullDump;

} ErrorTrackerHandlerInfo;

#define MAGIC_HANDLER_HEADER 0x13571357

#endif
