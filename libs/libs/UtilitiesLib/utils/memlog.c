#include "memlog.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "wininclude.h"
#include "utils.h"
#include "timing.h"
#include "UTF8.h"

MemLog g_genericlog = {0};
static volatile unsigned int g_memlog_uid;

void memlogEcho(const char* s, ...)
{
	va_list ap;
	char buf[MEMLOG_LINE_WIDTH+10];

	va_start(ap, s);
	if (vsprintf(buf,s,ap) < 10) return;
	va_end(ap);

	OutputDebugString_UTF8(buf);
	OutputDebugString(L"\n");
}

AUTO_RUN;
void memlog_defaultinit(void)
{
	//memlog_setCallback(&g_genericlog, memlogEcho);
	memlog_enableThreadId(&g_genericlog);
}

void memlog_init(MemLog* log)
{
	if (!log) log = &g_genericlog;
	memset(log, 0, sizeof(MemLog));
}

void memlog_setCallback(MemLog* log, Printf callback)
{
	if (!log) log = &g_genericlog;
	log->callback = callback;
}

void memlog_enableThreadId(MemLog* log)
{
	if (!log) log = &g_genericlog;
	log->careAboutThreadId = 1;
}


void memlog_vprintf(MemLog* log, char const *fmt, va_list ap)
{
	unsigned int old_value;

	PERFINFO_AUTO_START("memlog_printf", 1);
		if (!log)
			log = &g_genericlog;
		if (!log->dumping)
		{
			int len;
			old_value = ((U32)(InterlockedIncrement(&log->tail)-1))% MEMLOG_NUM_LINES;
			log->log[old_value].uid = InterlockedIncrement(&g_memlog_uid);
			log->log[old_value].threadid = log->careAboutThreadId?GetCurrentThreadId():0;

			len = vsprintf(log->log[old_value].text,fmt,ap);
			while (log->log[old_value].text[len-1]=='\n') {
				len--;
				log->log[old_value].text[len] = '\0';
			}
			log->log[old_value].text[MEMLOG_LINE_WIDTH-1] = 0;

			if (log->callback) {
				if (log->careAboutThreadId) {
					log->callback("[%d][%d] %s", log->log[old_value].uid, log->log[old_value].threadid, log->log[old_value].text);
				} else {
					log->callback("[%d] %s", log->log[old_value].uid, log->log[old_value].text);
				}
			}
			if (old_value == MEMLOG_NUM_LINES-1) {
				log->wrapped = true;
			}
		}
	PERFINFO_AUTO_STOP();
}

#undef memlog_printf
void memlog_printf(MemLog* log, char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	memlog_vprintf(log, fmt, ap);
	va_end(ap);
}

void memlog_dump(MemLog* log)
{
	unsigned int i;
	unsigned int eff_tail;
	
	if (!log)
		log = &g_genericlog;
	log->dumping = 1;
	eff_tail = log->tail % MEMLOG_NUM_LINES;
	OutputDebugStringf("Dumping memlog...\n");
	if (log->wrapped) {
		for (i=eff_tail; i<MEMLOG_NUM_LINES; i++) {
			if (log->careAboutThreadId) {
				OutputDebugStringf("[%6d][%5d] %s\n", log->log[i].uid, log->log[i].threadid, log->log[i].text);
			} else {
				OutputDebugStringf("[%6d] %s\n", log->log[i].uid, log->log[i].text);
			}
		}
	}
	for (i=0; i<eff_tail; i++) {
		if (log->careAboutThreadId) {
			OutputDebugStringf("[%6d][%5d] %s\n", log->log[i].uid, log->log[i].threadid, log->log[i].text);
		} else {
			OutputDebugStringf("[%6d] %s\n", log->log[i].uid, log->log[i].text);
		}
	}
	OutputDebugStringf("Done.\n");
	log->dumping = 0;
}
