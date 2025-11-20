/***************************************************************************



***************************************************************************/

#include "svrError.h"
#include "error.h"

#include "winutil.h"
#include "sysutil.h"
#include "logcomm.h"
#include "ServerLib.h"
#include "timing.h"
#include "Alerts.h"
#include "file.h"
#include "hoglib.h"
#include "utils.h"

static void addErrorToQueue(char *str);

//------------------------------------------------------------
// Error callbacks
//------------------------------------------------------------
static void GimmeErrorDialog(ErrorMessage* errMsg)
{
	char *errString;
	char author[200];

	PERFINFO_AUTO_START_FUNC();

	errString = errorFormatErrorMessage(errMsg);
	author[0] = 0;

	if (errMsg->author)
	{
		if (strlen(errMsg->author) < 15)
			sprintf(author, "%s is Responsible", errMsg->author);
		else
			strcpy(author, errMsg->author);
	}
	errorDialog(compatibleGetConsoleWindow(), errString, 0, author[0]? author: NULL, errMsg->bForceShow);	

	PERFINFO_AUTO_STOP();
}


int noPopUps;
// Disables pop-up errors.  This should only be used in case of an emergency,
//  instead fix the code to behave reasonably in production mode and run with
//  -production mode.
AUTO_CMD_INT(noPopUps, noPopUps2) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE;

// Add this flag to disable printing errors to the console.
bool noPrintErrorsToConsole = false;
AUTO_CMD_INT(noPrintErrorsToConsole, noPrintErrorsToConsole) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE;

#define MAX_ERRORS_OUT	200

void serverErrorfCallback(ErrorMessage* errMsg, void *userdata)
{
	char *errString;
	
	PERFINFO_AUTO_START_FUNC();
	
	errString = errorFormatErrorMessage(errMsg);

	// Print the error to the console.
	if (!noPrintErrorsToConsole)
	{
		if (!consoleIsCursorOnLeft())
			printf("\n");
		printfColor(COLOR_RED|COLOR_GREEN, "ERROR: %s\n", errString);
	}

	// Pop up an error dialog.
	if (!noPopUps && errMsg->errorCount < MAX_ERRORS_OUT && errMsg->bRelevant && gServerLibState.bAllowErrorDialog)
		GimmeErrorDialog(errMsg);

	// Log the error.
	log_printf(LOG_BUG, "(Server Error Msg):\"%s\" in %s:%d", errString, NULL_TO_EMPTY(errMsg->file), errMsg->line);	

	PERFINFO_AUTO_STOP();
}

//------------------------------------------------------------
// Error queuing
//------------------------------------------------------------
static int error_queue_count,error_queue_max;
static char *error_queue;

static void addErrorToQueue(char *str)
{
	int len = (int)strlen(str);
	char	*s;

	s = dynArrayAdd(error_queue,1,error_queue_count,error_queue_max,len + 1);
	strcpy_s(s,len+1,str);
	s[len] = 0;
}

char *GetQueuedError()
{
	char	*s;
	static	int		curr_pos;

	if (!error_queue_count)
		return 0;
	s = error_queue + curr_pos;
	curr_pos += (int)strlen(s)+1;
	if (curr_pos >= error_queue_count)
	{
		error_queue_count = 0;
		curr_pos = 0;
	}
	return s;
}

//------------------------------------------------------------
// Implementation limits
//------------------------------------------------------------

// Emit hog file warning.
static void svrHogWarning(const char *hog, const char *percent, U32 file_count)
{
	if (isDevelopmentMode())
		assertmsgf(0, "%s exceeded safe hog limit", hog);
	if (percent)
		TriggerAlertDeferred("HOG_FILE_COUNT_LIMIT_NEARING", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, "%s (%lu files) has exceeded %s%% of the maximum operating limit for the number of files in a hog, 100,000,000.",
			hog, file_count, percent);
	else
		TriggerAlertDeferred("HOG_FILE_COUNT_LIMIT_EXCEEDED", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, "%s (%lu files) has exceeded the maximum operating limit for the number of files in a hog, 100,000,000.  SYSTEM FAILURE IS IMMINENT!  NetOps must act now to prevent data loss and catastrophic failure.\n\nBrigadier General Thomas Farrell relayed the following account of the Cryptic hog system overloading testing at Los Alamos: \"Dr. Oppenheimer, on whom had rested a very heavy burden, grew tenser as the last seconds ticked off. He scarcely breathed. He held on to a post to steady himself. For the last few seconds, he stared directly ahead and then when the announcer shouted 'Now!' and there came this tremendous burst of light followed shortly thereafter by the deep growling roar of the explosion, his face relaxed into an expression of tremendous relief.\"",
			hog, file_count);
}

// Too many files in a hog.
U32 OVERRIDE_LATELINK_hogWarnImplementationLimitExceeded(const char *hog, U32 implementation_limit_warning, U32 file_count)
{
	if (file_count > 50000000 && implementation_limit_warning < file_count)
	{
		svrHogWarning(hog, "50", file_count);
		implementation_limit_warning = 75000000;
	}
	if (file_count > 75000000 && implementation_limit_warning < file_count)
	{
		svrHogWarning(hog, "75", file_count);
		implementation_limit_warning = 90000000;
	}
	if (file_count > 90000000 && implementation_limit_warning < file_count)
	{
		svrHogWarning(hog, "90", file_count);
		implementation_limit_warning = 100000000;
	}
	if (file_count > 100000000 && implementation_limit_warning < file_count)
	{
		svrHogWarning(hog, NULL, file_count);
		implementation_limit_warning = UINT_MAX;
	}

	return implementation_limit_warning;
}
