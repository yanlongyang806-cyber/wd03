#ifndef ACCOUNTSERVERCOMMANDS_H
#define ACCOUNTSERVERCOMMANDS_H

int TestAccountLogin(const char *pAccountName, const char *pPassword);

typedef struct AccountInfoStripped AccountInfoStripped;

char * assignKeyWithPassword(const char *pAccountName, const char *pPassword, const char *pKey, bool bNewAccount);

AccountInfoStripped * WebInterfaceGetAccountInfo(const char *pAccountName);
char * confirmLogin(const char *pAccountName, const char *pPassword);

void createKeyGroup (const char * prefix, const char * productName, const char * defaultPermissionString, int defaultAccessLevel);

void createKeys(const char * prefix, int keyCount);

void getUnassignedKey(const char * prefix);

void assignKey(char *pAccountName, char *pKey);

void printCmd (char *cmdString);

void printAllCmds(void);

void reconnect(void);

#endif