#pragma once
GCC_SYSTEM

C_DECLARATIONS_BEGIN

#define MEMLOG_NUM_LINES 256
#define MEMLOG_LINE_WIDTH 256

typedef void (*Printf)(FORMAT_STR const char *, ...);
typedef struct MemLogLine
{
	unsigned int uid;
	unsigned int threadid;
	char text[MEMLOG_LINE_WIDTH];
} MemLogLine;

typedef struct MemLog 
{
	MemLogLine log[MEMLOG_NUM_LINES];
	volatile unsigned long tail;
	int wrapped;
	Printf callback;
	int careAboutThreadId;
	int dumping;
} MemLog;

void memlog_init(MemLog* log);
void memlog_enableThreadId(MemLog* log);
void memlog_vprintf(MemLog* log, char const *fmt, va_list ap);
void memlog_printf(MemLog* log, FORMAT_STR char const *fmt, ...);
#define memlog_printf(log, fmt, ...) memlog_printf(log, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void memlog_dump(MemLog* log);
void memlog_setCallback(MemLog* log, Printf callback);

C_DECLARATIONS_END

