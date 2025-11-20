/* Routines for running mergers shared among all programs that use CrypticDB stores.
 *
 * Besides the per-shard ObjectDB, many top-level servers also run their own CrypticDB, generally copy-pasting
 * all of the common infrastructure.  This includes the code for starting the merger and running the merge itself.
 * In the future, all new shared merger code should be put into this file.  Ideally, the old copy-pasted code
 * could be migrated here as well at some point.
 */

#ifndef CRYPTIC_OBJMERGER_H
#define CRYPTIC_OBJMERGER_H

// Return true if a merger is running.
bool IsMergerRunning(const char *pMergerName);

// Acquire a lock that indicates that the specified merger is running.  Return false if one is already running.
bool LockMerger(const char *pMergerName);

// Release the merger lock, indicating that the merger is no longer running.
void UnlockMerger(void);

#endif  // CRYPTIC_OBJMERGER_H
