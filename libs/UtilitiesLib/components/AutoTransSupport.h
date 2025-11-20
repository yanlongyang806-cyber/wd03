#pragma once
typedef struct ParseTable ParseTable;
typedef struct TransactionReturnVal TransactionReturnVal;

//returns an earray of strings
char ***GetListOfFieldsThatAreAlwaysIncluded(ParseTable containerTPI[]);

//writes a specially coded string which includes the address of the struct, and will
//be replaced by ParserWriteText if the transaction is going to be executed remotely
void AutoTrans_WriteLocalStructString(ParseTable *pTPI, const void *pStructPtr, char **ppOutString);

//given a string which may have one or more of the above in it, replaces all LocalStructStrings
//with ParserWriteText, returns number of substitutions made
int AutoTrans_FixupLocalStructStringIntoParserWriteText(char **ppFixupString);


void *AutoTrans_ParserReadTextEscapedOrMaybeFromLocalStructString(ParseTable *pTPI, char *pString, 
	bool bReturnLocalCopyWithNoCloneIfPossible, bool *pbOutReturnedLocalCopy);


//given an auto trans function name, a server type it will be executed on, and a return val, 
//verifies that if the AUTO_TRANS is going to do logging returns, the return val is 
//properly going to be able to process them
void AutoTrans_VerifyReturnLoggingCompatibility(const char *pAutoTransFuncName, GlobalType eTypeToExecuteOn, 
	TransactionReturnVal *pReturnVal);


LATELINK;
void RemotelyVerifyNoReturnLogging(const char *pAutoTransFuncName, GlobalType eTypeToExecuteOn);

//called after a latelink calls a remote command, because utilitiesLib can't have remote commands. Doo de doo.
void FinalCallRemoteVerifyNoReturnLogging(char *pAutoTransFuncName, GlobalType eCallingType);