#ifndef ERRORTRACKER_EMAIL_H
#define ERRORTRACKER_EMAIL_H

typedef struct ErrorEntry ErrorEntry;
typedef struct NOCONST(ErrorEntry) NOCONST(ErrorEntry);

#define MAX_SVN_BLAME_DEPTH (3)

void AddEmailToQueue(char *pWhoTo, char *pTitle, char *pFileName);
void ProcessEmailQueue(void);
void SendErrorEmail(char *pUserToSendTo, char *pErrorString, U32 iDataFileModificationDate, char *pWhoGotIt, char *pWhoElseGotIt, char *pExecutableName);
void PurgeErrorEmailTimesTable(void);

void sendEmailsOnNewBlame(ErrorEntry *pBlameEntry, int indexFlags);
void sendEmailsOnNewError(ErrorEntry *pNewEntry, ErrorEntry *pMergedEntry);

void SendEmailFromStrings(const char *pWhoTo, const char *pTitle, char *pStr1, char *pStr2);
void SendNightlyEmails(void);

bool RunningFromToolsBin(ErrorEntry *p);
bool ArtistGotIt(ErrorEntry *p);

const char * getMachineAddress(void);

void notifyDumpReceived(ErrorEntry *p);
void ClearEmailFiles(void);

void SendNightlyCrashCount(void);

// Unknown IP stuff
void UnknownIPSaveFile(void);
void UnknownIPLoadFile(void);
void SendUnknownIPEmail(void);
void AddUnknownIP(U32 uIP, ErrorEntry *pEntry, const U32 uEntryID);

// Symbol Lookup Failure
void AddSymbolLookupFailure(SA_PARAM_NN_STR const char *module, SA_PARAM_NN_STR const char *guid);
void SendSymbolLookupEmail(void);

#endif
