#include "logging.h"
#include "rand.h"
#include "PerfLogger.h"
#include "ThreadManager.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "wininclude.h"

// Size of test_buffer
#define TEST_BUFFER_SIZE 1024

// Test buffer
static char *test_buffer = NULL;

// Adler-32, copied from CrypticPerfTest.
// This local copy is used for consistent results even if our primary Adler-32 implementation needs to be changed.
__forceinline void localAdler32CalculateCRC(const U8 *input, unsigned int length, U8 *hash)
{
	static const unsigned long BASE = 65521;

	unsigned long s1 = 1;
	unsigned long s2 = 0;
	U16 m_s1, m_s2;

	if (length % 8 != 0)
	{
		do
		{
			s1 += *input++;
			s2 += s1;
			length--;
		} while (length % 8 != 0);

		if (s1 >= BASE)
			s1 -= BASE;
		s2 %= BASE;
	}

	while (length > 0)
	{
		s1 += input[0]; s2 += s1;
		s1 += input[1]; s2 += s1;
		s1 += input[2]; s2 += s1;
		s1 += input[3]; s2 += s1;
		s1 += input[4]; s2 += s1;
		s1 += input[5]; s2 += s1;
		s1 += input[6]; s2 += s1;
		s1 += input[7]; s2 += s1;

		length -= 8;
		input += 8;

		if (s1 >= BASE)
			s1 -= BASE;
		if (length % 0x8000 == 0)
			s2 %= BASE;
	}

	assert(s1 < BASE);
	assert(s2 < BASE);

	m_s1 = (U16)s1;
	m_s2 = (U16)s2;

	hash[3] = (U8)(m_s1);
	hash[2] = (U8)(m_s1 >> 8);
	hash[1] = (U8)(m_s2);
	hash[0] = (U8)(m_s2 >> 8);
}

// Adler-32 speed test, copied from CrypticPerfTest.
// This local copy is used for consistent results even if CrypticPerfTest changes.
F32 adlerSpeedTest(void *data_buf, int timer, U32 size, F32 maxtime)
{
	int i;
	F32 ret;
	U32 hash = 0;
	timerStart(timer);
	for (i=0; i<1500000 && timerElapsed(timer) < maxtime; i++)
		localAdler32CalculateCRC(data_buf, size, (U8*)&hash);
	ret = size * (F32)i / timerElapsed(timer);
	return ret / (1024*1024);
}

// Run a brief performance test, and save the result.
static void PerfLoggerTest(const char *name)
{
	int timer;
	F32 result;

	PERFINFO_AUTO_START_FUNC();
	timer = timerAlloc();
	result = adlerSpeedTest(test_buffer, timer, TEST_BUFFER_SIZE, 1);
	timerFree(timer);
	SERVLOG_PAIRS(LOG_FRAMEPERF, name, ("result", "%f", result));
	PERFINFO_AUTO_STOP_FUNC();
}

// The delay is random to be independent with respect to periodic performance effects.
static void PerfLoggerWait(void)
{
	U32 delay;

	PERFINFO_AUTO_START_FUNC();
	delay = randomIntRange(30*1000, 90*1000);
	Sleep(delay);
	PERFINFO_AUTO_STOP_FUNC();
}

// Loop forever running performance test.
static DWORD WINAPI PerfLoggerThread(LPVOID lpParam)
{
	const char *name = lpParam;

	EXCEPTION_HANDLER_BEGIN;

	for (;;)
	{
		autoTimerThreadFrameBegin(__FUNCTION__);

		// Wait a while.
		PerfLoggerWait();

		// Run the perf test.
		PerfLoggerTest(name);

		autoTimerThreadFrameEnd();
	}

	EXCEPTION_HANDLER_END;
}

// Run the perflogger.
void perfloggerInit()
{
	static ManagedThread *thread = NULL;
	static ManagedThread *thread_low = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Initialize thread, if it doesn't exist yet.
	if (!thread)
	{
		int i;
		BOOL result;

		// Initialize random test buffer.
		test_buffer = malloc(TEST_BUFFER_SIZE);
		for (i = 0; i != TEST_BUFFER_SIZE; ++i)
			test_buffer[i] = randomInt() % 256;

		// Create regular priority test thread.
		thread = tmCreateThread(PerfLoggerThread, "AdlerTest");
		result = SetThreadPriority(tmGetThreadHandle(thread), THREAD_PRIORITY_LOWEST);  // If the Launcher is ABOVE_NORMAL_PRIORITY_CLASS,
																						// this should be the same as THREAD_PRIORITY_NORMAL
																						// in NORMAL_PRIORITY_CLASS on a Game Server.
		if (!result)
			WinErrorf(GetLastError(), "Unable to set normal priority");

		// Create low priority test thread.
		thread_low = tmCreateThread(PerfLoggerThread, "AdlerTestLow");
		result = SetThreadPriority(tmGetThreadHandle(thread_low), THREAD_PRIORITY_IDLE);
		if (!result)
			WinErrorf(GetLastError(), "Unable to set low priority");
	}

	PERFINFO_AUTO_STOP();
}
