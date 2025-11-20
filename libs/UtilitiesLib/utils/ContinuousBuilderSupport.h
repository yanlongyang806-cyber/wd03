#pragma once

//code for sending status updates and other such stuff to the continuous builder.



//if true, this process was launched by the continuous builder (or launched by something launched by, etc.)
extern bool g_isContinuousBuilder;

//types of strings to send to the CB
typedef enum CBStringType
{
	//send a comment string to the CB. This will show up on the CB page, be logged, and can also
	CBSTRING_COMMENT,

	//DO NOT USE THESE, they are for controller scripting only
	CBSTRING_SUBSTATE,
	CBSTRING_SUBSUBSTATE,
} CBStringType;

void SendStringToCB(CBStringType eType, char *pString, ...);

char *GetCBDataCorruptionComment(void);

void CBSupport_StartErrorTracking(void);
void CBSupport_StopErrorTracking(void);

void CBSupport_PauseTimeout(int iNumSeconds);

//if one thing talking to the CB spawns another thing that is talking to the CB, it should
//append this command line
char *CBSupport_GetSpawningCommandLine(void);