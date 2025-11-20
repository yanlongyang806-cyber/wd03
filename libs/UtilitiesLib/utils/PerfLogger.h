// Random periodic performance logger

// Intended for tracking performance degradation due to CPU contention, this code will log actions AdlerTest
// and AdlerTestLow (test run using low priority) to category FRAMEPERF.  This is designed to be run by the
// Launcher.

#ifndef CRYPTIC_PERFLOGGER_H
#define CRYPTIC_PERFLOGGER_H

// Run the perflogger.
void perfloggerInit(void);

#endif  // CRYPTIC_PERFLOGGER_H
