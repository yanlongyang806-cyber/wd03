//this .c file is here because AUTO_ stuff doesn't work in .cpp files
extern char userName[64];

extern int sendLogToTrackerDumpFlags;

AUTO_COMMAND;
void SetUserName(char *pUserName)
{
	strcpy(userName, pUserName);
}

AUTO_COMMAND;
void setSendLogToTrackerDumpFlags(int flags)
{
	sendLogToTrackerDumpFlags = flags;
}
