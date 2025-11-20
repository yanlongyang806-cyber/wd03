#ifndef ERROR_H
#define ERROR_H
#pragma once
GCC_SYSTEM

#include <stdarg.h>
#include "stdtypes.h"

//#define MAKE_ERRORS_LESS_ANNOYING 1

C_DECLARATIONS_BEGIN


#ifdef _IWantSyntaxHighlighting_
typedef int __VA_ARGS__;
#endif

// Public use macros and functions

// Reports an error for logging
#if SPU
#define Errorf(fmt, ...)  assert(0)
#define ErrorGroupedByFilef(fmt, ...)  assert(0)
#else
#define Errorf(fmt, ...)  ErrorfInternal(true, __FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define ErrorGroupedByFilef(fmt, ...)  ErrorfInternal(true, __FILE__, 0, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#endif

// Reports an error, and marks the thing being validated as invalid
#define InvalidDataErrorf(fmt, ...)  InvalidDataErrorfInternal(__FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Reports an error at save time, but allow loading of data without errors
#define DeprecatedErrorf(fmt, ...)  DeprecatedErrorfInternal(__FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Behaves like Errorf but doesn't cause an error to be logged. This is only
// useful in very rare cases. It should never be used to report errors in
// actual data files.
#define Alertf(fmt, ...) { if (!gbDontDoAlerts) AlertfInternal(__FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__); }
void AlertfInternal(char *pFile, int iLine, char *pFmt, ...);


// Reports error caused by passed in filename
#define ErrorFilenamef(filename, fmt, ...)  ErrorFilenamefInternal(__FILE__, __LINE__, filename, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

//like the above, but does the actual reporting from a worker thread so the main thread doesn't stall while talking to asset master
#define ErrorFilenamef_NonBlocking(filename, fmt, ...)  ErrorFilenamef_NonBlockingInternal(__FILE__, __LINE__, filename, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void ErrorFilenamev_NonBlockingInternal(const char *file, int line, const char *filename, char const *fmt, va_list ap);

// Reports error caused by passed in filename
#define InvalidDataErrorFilenamef(filename, fmt, ...)  InvalidDataErrorFilenamefInternal(__FILE__, __LINE__, filename, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// group is a null terminated, space comma or pipe("|") separated list of groups 
//
// days is the number of days it will be displayed to only that group, after which it will
// be displayed to everyone.  -1 for infinite
#define ErrorFilenameGroupf(filename, group, days, fmt, ...) ErrorFilenameGroupfInternal(__FILE__, __LINE__, filename, group, days, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Same as above, but passes ERROR_INVALID instead of ERROR_NORMAL to invalidate data on load
#define ErrorFilenameGroupInvalidDataf(filename, group, days, fmt, ...) ErrorFilenameGroupInvalidfInternal(__FILE__, __LINE__, filename, group, days, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

//same as the above, but instead of using the modification date of the file, uses a specified date
//(year can be 2 or 4 digits)
#define ErrorFilenameGroupRetroactivef(filename, group, days, curMonth, curDay, curYear, fmt, ...) ErrorFilenameGroupRetroactivefInternal(__FILE__, __LINE__, filename, group, days, curMonth, curDay, curYear, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Reports error on two separate files
#define ErrorFilenameTwof(filename1, filename2, fmt, ...)  ErrorFilenameTwofInternal(__FILE__, __LINE__, filename1, filename2, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Reports duplicate object error
#define ErrorFilenameDup(filename1, filename2, key, type)  ErrorFilenameDupInternal(__FILE__, __LINE__, filename1, filename2, key, type)

// Force sending the callstack - IS EXTREMELY SLOW
#define ErrorfForceCallstack(fmt, ...) ErrorfCallstackInternal(true, __FILE__, __LINE__, false, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Deferred version of above macros, that will get called next frame in main thread
#define ErrorFilenameDeferredf(filename, fmt, ...) ErrorFilenameDeferredfInternal(__FILE__, __LINE__, filename, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define ErrorFilenameGroupDeferredf(filename, group, fmt, ...) ErrorFilenameGroupDeferredfInternal(__FILE__, __LINE__, filename, group, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define ErrorFilenameGroupRetroactiveDeferredf(filename, group, days, curmon, curday, curyear, fmt, ...) ErrorFilenameGroupRetroactiveDeferredfInternal(__FILE__, __LINE__, filename, group, days, curmon, curday, curyear, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Deferred version of Errorf, that will get called next frame in main thread
#define ErrorDeferredf(fmt, ...) ErrorDeferredfInternal(__FILE__, __LINE__, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Reports a fatal error, which causes the program to abort
#if SPU
#define FatalErrorf(fmt, ...)  assert(0)
#else
void FatalErrorf(FORMAT_STR char const *fmt, ...);
#define FatalErrorf(fmt, ...) FatalErrorf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#endif
void FatalErrorFilenamef(const char *filename, FORMAT_STR char const *fmt, ...);
#define FatalErrorFilenamef(filename, fmt, ...) FatalErrorFilenamef(filename, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void FatalErrorv(const char* fmt, va_list ap);

// Initialize callbacks used by the error system

AUTO_ENUM;
typedef enum ErrorMessageType {
	ERROR_NORMAL,	
	ERROR_DEPRECATED,
	ERROR_INVALID,
	ERROR_FATAL,
	ERROR_ALERT
} ErrorMessageType;

AUTO_STRUCT;
typedef struct ErrorMessage {
	ErrorMessageType errorType;
	char *estrFormattedMsg;	AST(ESTRING)     // used internally by errorFormatErrorMessage
	char *estrMsg;			AST(ESTRING)
	char *estrDetails;		AST(ESTRING)
	const char *filename;	AST(POOL_STRING) //this is the gimme file


	const char *file;		AST(POOL_STRING) //this is the source code file
	int line; //note that this is the line number in the SOURCE FILE not the data file.
	const char *group;		AST(POOL_STRING)
	char *author;
	int days;
	int curday;
	int curmon;
	int curyear;
	bool bRelevant;
	bool bForceSendStack;
	bool bForceShow;
	bool bReport;
	bool bReported;
	bool bNotLatestData;
	bool bCheckedOut;
	int errorCount;

	bool bSent; AST(NAME(sent))
} ErrorMessage;

extern ParseTable parse_ErrorMessage[];
#define TYPE_parse_ErrorMessage ErrorMessage

typedef void (*ErrorCallback)(ErrorMessage *errMsg, void *userdata);

// Will return a function static string with the default formatting of an ErrorMessage
// Also sends the ErrorMessage to ErrorTracker
// Meant to be used by errorf callbacks
char *errorFormatErrorMessage(ErrorMessage *errMsg);


void defaultErrorCallback(ErrorMessage *errMsg, void *userdata);
void defaultFatalErrorCallback(ErrorMessage *errMsg, void *userdata);


void FatalErrorfSetCallback(ErrorCallback func, void *userdata);
void ErrorfSetCallback(ErrorCallback func, void *userdata);
void ErrorfPushCallback(ErrorCallback func, void *userdata);
void ErrorfPopCallback(void);
void ErrorfCallCallback(ErrorMessage *errMsg);

//to redirect all errorfs into an estring, call ErrorfPushCallback with this and your estring as the second arg
void EstringErrorCallback(ErrorMessage *errMsg, void *userdata);


// Verbose printing and logging functions
void verbose_printf(FORMAT_STR const char *fmt, ...);
#define verbose_printf(fmt, ...) verbose_printf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void errorSetVerboseLevel(int v);
void errorSetTempVerboseLevel(int v);
int errorGetVerboseLevel(void);
void loadstart_printf(FORMAT_STR const char* fmt, ...);
#define loadstart_printf(fmt, ...) loadstart_printf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void loadupdate_printf(FORMAT_STR const char* fmt, ...);
#define loadupdate_printf(fmt, ...) loadupdate_printf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void loadend_printf(FORMAT_STR const char* fmt, ...);
#define loadend_printf(fmt, ...) loadend_printf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
typedef void (*voidVoidFunc)(void);
void loadend_setCallback(voidVoidFunc callback);
void loadstart_report_unaccounted(bool shouldReport);
void loadstart_suppress_printing(bool shouldSuppress);
int loadstart_depth(void);
void setLoadTimingPrecistion(int digits);
#define loadendstart_printf(end, start, ...) loadend_printf(end); loadstart_printf(start, __VA_ARGS__)
#define loadrestart_printf(start, ...) loadendstart_printf("done.", start, __VA_ARGS__)

// Get access to Windows errors

// Format a Windows error code as an estring.
char *getWinErrString(char **pestrString, int iError);

// Print a Windows error string to stdout.
void printWinErr(const char* functionName, const char* filename, int lineno, int err);

// Format the last Windows error to a static buffer.
char *lastWinErr(void);

// Like Errorf(), but with ": " and the formatted error information appended.
void WinErrorfInternal(bool bCreateReport, const char *file, int line, int code, FORMAT_STR char const *fmt, ...);
#define WinErrorf(code, fmt, ...) WinErrorfInternal(true, __FILE__, __LINE__, code, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Internal use functions used by above macros
void DeprecatedErrorfInternal(const char *file, int line, FORMAT_STR char const *fmt, ...);
void InvalidDataErrorfInternal(const char *file, int line, FORMAT_STR char const *fmt, ...);
void ErrorfInternal(bool bCreateReport, const char *file, int line, FORMAT_STR char const *fmt, ...);
void ErrorvInternal(bool bCreateReport, const char *file, int line, FORMAT_STR char const *fmt, va_list ap);
void ErrorFilenamefInternal(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, ...);
void ErrorFilenamef_NonBlockingInternal(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, ...);
void InvalidDataErrorFilenamefInternal(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, ...);
void DeprecatedErrorFilenamefInternal(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, ...);
void ErrorFilenamevInternal(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, va_list ap);
void ErrorFilenameTwofInternal(const char *file, int line, const char *filename1, const char *filename2, FORMAT_STR char const *fmt, ...);
void ErrorFilenameDupInternal(const char *file, int line, const char *filename1, const char *filename2, const char *key, const char *type);
void ErrorvCallstackInternal(bool bCreateReport, const char *file, int line, bool bDoNotSendCallstack, FORMAT_STR char const *fmt, va_list ap);
void ErrorfCallstackInternal(bool bCreateReport, const char *file, int line, bool bDoNotSendCallstack, FORMAT_STR char const *fmt, ...);

// Deferred forms
void ErrorDeferredfInternal(const char *file, int line, FORMAT_STR char const *fmt, ...);
void ErrorFilenameDeferredfInternal(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, ...);
void ErrorFilenameGroupDeferredfInternal(const char *file, int line, const char *filename, const char *group, FORMAT_STR char const *fmt, ...);
void ErrorFilenameGroupRetroactiveDeferredfInternal(const char *file, int line, const char *filename, const char *group, int days, int curmon, int curday, int curyear, FORMAT_STR char const *fmt, ...);
void ErrorFilenameGroupfInternal(const char *file, int line, const char *filename, const char * group, int days, FORMAT_STR char const *fmt, ...);
void ErrorFilenameGroupRetroactivefInternal(const char *file, int line, const char *filename, const char *group, int days,
	int curMonth, int curDay, int curYear, FORMAT_STR char const *fmt, ...);
void ErrorFilenameGroupInvalidfInternal(const char *file, int line, const char *filename, const char * group, int days, FORMAT_STR char const *fmt, ...);


// Computes author based on file name
void errorFindAuthor(ErrorMessage *errMsg);

// is the current user in the error group
int UserIsInGroup(const char *group);
int UserIsInGroupEx(const char *group, bool disableSpecialGroups);

// is the given string a group?
int IsGroupName(const char *group);

// Fill the given string array with all of the users
void FillAllUsers(char ***users);

// Fill the given string array with all of the unique groups
void FillAllGroups(char ***groups);

// Info on number of errors displayed so far
void ErrorfResetCounts(void);

// Deals with any deferred errors
void ErrorOncePerFrame(void);

void errorLogStart(void);
void errorLogFileHasError(const char *file);
void errorLogFileIsBeingReloaded(const char *file);

extern bool g_disableLastAuthor;
extern char* g_lastAuthorIntro;

extern bool gbDontReportErrorsToErrorTracker;
extern bool gbDontDoAlerts;

void errorDoNotFreezeThisThread(unsigned long threadID);
void pushDontReportErrorsToErrorTracker(bool newValue);
void popDontReportErrorsToErrorTracker(void);
void pushDisableLastAuthor(bool newValue);
void popDisableLastAuthor(void);

void dontLogErrors(bool bDontLogErrors);


void errorShutdown(void);


//special error function for things which are very very serious but shouldn't actually assert in production mode...
//Asserts in dev mode, triggers an alert in prod mode
void AssertOrAlertEx(SA_PARAM_NN_STR const char *pKeyString, SA_PARAM_NN_STR const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...);
#define AssertOrAlert(pKeyString, pErrorStringFmt, ...) AssertOrAlertEx(pKeyString, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)
void AssertOrAlertWarningEx(SA_PARAM_NN_STR const char *pKeyString, SA_PARAM_NN_STR const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...);
#define AssertOrAlertWarning(pKeyString, pErrorStringFmt, ...) AssertOrAlertWarningEx(pKeyString, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)
void AssertOrProgrammerAlertEx(SA_PARAM_NN_STR const char *pKeyString, SA_PARAM_NN_STR const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...);
#define AssertOrProgrammerAlert(pKeyString, pErrorStringFmt, ...) AssertOrProgrammerAlertEx(pKeyString, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)


void AssertOrAlertWithStructEx(const char *pKeyString, ParseTable *pTPI, void *pStruct, const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...);
#define AssertOrAlertWithStruct(pKeyString, pTPI, pStruct, pErrorStringFmt, ...) AssertOrAlertWithStructEx(pKeyString, pTPI, pStruct, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)
void AssertOrAlertWarningWithStructEx(const char *pKeyString, ParseTable *pTPI, void *pStruct, const char *pFileName, int iLineNum, const char *pErrorStringFmt, ...);
#define AssertOrAlertWarningWithStruct(pKeyString, pTPI, pStruct, pErrorStringFmt, ...) AssertOrAlertWarningWithStructEx(pKeyString, pTPI, pStruct, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)

//errorf in dev mode, alerts in production mode
void ErrorOrAlertEx(SA_PARAM_NN_STR const char *pKeyString, SA_PARAM_NN_STR const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...);
#define ErrorOrAlert(pKeyString, pErrorStringFmt, ...) ErrorOrAlertEx(pKeyString, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)
void ErrorOrProgrammerAlertEx(SA_PARAM_NN_STR const char *pKeyString, SA_PARAM_NN_STR const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...);
#define ErrorOrProgrammerAlert(pKeyString, pErrorStringFmt, ...) ErrorOrProgrammerAlertEx(pKeyString, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)

void ErrorOrAutoGroupingAlert(const char *pKeyString, int iGroupingSeconds, FORMAT_STR const char *pErrorStringFmt, ...);


//errorf in dev mode, critical alerts in production mode
void ErrorOrCriticalAlertEx(SA_PARAM_NN_STR const char *pKeyString, SA_PARAM_NN_STR const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...);
#define ErrorOrCriticalAlert(pKeyString, pErrorStringFmt, ...) ErrorOrCriticalAlertEx(pKeyString, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)

//Same as above, but puts it in a list that won't actually get sent out until next frame. In case you're trying
//to call from deep inside the networking code or something like that.
//
//if bCriticalAlert is true, then a critical alert is always generated, otherwise ErrorOrAlert is called
void ErrorOrAlertDeferred(bool bCriticalAlert, SA_PARAM_NN_STR const char *pKeyString, FORMAT_STR const char *pErrorStringFmt, ...);
#define ErrorOrAlertDeferred(bCriticalAlert, pKeyString, pErrorStringFmt, ...) \
	ErrorOrAlertDeferred(bCriticalAlert, pKeyString, FORMAT_STRING_CHECKED(pErrorStringFmt), __VA_ARGS__)



// use ErrorDetailsf() to split the random, data-driven part of an error from the "bucketable" part
// example: "Item has fallen out of the world [Pos: (x,y,z)]" will create a separate ET entry for each one
//          if you want all all item-falling-out-of-world to be the same ET entry, instead do:
//          ErrorDetailsf("[Pos: (x,y,z)]");
//          Errorf("Item has fallen out of the world!");
// Note: call BEFORE any of the Errorf calls
void ErrorDetailsf(FORMAT_STR const char *pDetailsStringFmt, ...);
#define ErrorDetailsf(pDetailsStringFmt, ...) ErrorDetailsf(FORMAT_STRING_CHECKED(pDetailsStringFmt), __VA_ARGS__)


// For tracking if the crash should trigger a full verify
void errorIsDuringDataLoadingInc(SA_PARAM_OP_STR const char *filename);
void errorIsDuringDataLoadingDec(void);
bool errorIsDuringDataLoadingGet(void);
const char *errorIsDuringDataLoadingGetFileName(void);

void ErrorStartThreadTest(const char *errormsg);

//on servers inside a dev shard, we always report the error, even if it's a duplicate, so the
//count column on the MCP error window is correct. Note that this does not cause multiple reporting to errortracker
void ErrorSetAlwaysReportDespiteDuplication(bool bSet);


//for complicated reasons, there are certain situations in which objects loaded from a filename with one extension have the
//extension of their internal filename changed to another extension. For error reporting purposes, when an error is reported
//associated with one of these filenames, if it doesn't exist, but the paired extension file does, then report on that other
//filename. So for instance, if we want to report an error on foo.rootmods, and foo.rootmods doesn't exist, but foo.modelnames does
//exist, then report the error as coming from foo.modelnames
void ErrorAddInterchangeableFilenameExtensionPair(char *pStr1, char *pStr2);

C_DECLARATIONS_END

#endif
