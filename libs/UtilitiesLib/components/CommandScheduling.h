#ifndef COMMANDSCHEDULING_H_
#define COMMANDSCHEDULING_H_
GCC_SYSTEM

#define EVERY_INTERVAL -1

AUTO_ENUM;
typedef enum {
	DOW_ALL = -1,	ENAMES(Everyday All)
	DOW_SUN = 0,	ENAMES(Sunday Sun)
	DOW_MON = 1,	ENAMES(Monday Mon)
	DOW_TUE = 2,	ENAMES(Tuesday Tue)
	DOW_WED = 3,	ENAMES(Wednesday Wed)
	DOW_THU = 4,	ENAMES(Thursday Thu)
	DOW_FRI = 5,	ENAMES(Friday Fri)
	DOW_SAT = 6,		ENAMES(Saturday Sat)
} DayOfWeek;

//A cron-like schedule definition
AUTO_STRUCT;
typedef struct RecurringSchedule
{
	int minute;			//The minute of the hour to execute on.
	int hour;			//The hour of the day to execute on.
	int dom;			//The day of the month to execute on.
	int month;			//The month of the year to execute on.
	DayOfWeek dow;			//The day of the week to execute on.
	char *command;		//The command to issue.
} RecurringSchedule;




#endif