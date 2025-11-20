#pragma once
GCC_SYSTEM

// The functions in this file should be used as the low-level mechanism to get file timestamps, instead of using the MS CRT (stat(), etc), which has a history of bugs.
// They get file times directly from the Windows API, using some code based on Perl.

#if _PS3

//YVS
__forceinline static bool _FileTimeToUnixTime(
    const FILETIME  *ft,
    __time32_t          *ut,
    const bool      ft_is_local
) {
    *ut = ft->time;
    return 1;
}

__forceinline static bool _UnixTimeToFileTime(
	const __time32_t    ut,
	FILETIME        *ft,
    const bool      make_ft_local
) {
    ft->time = ut;
    return 1;
}

#define _AltStat _stat32

#else

#include "windefinclude.h"

int _AltStat(const char *name, 
    struct _stat32i64      *st_buf
	);

//passing 0 for any of Created/Modified/Accessed means "Do not change"
int _SetUTCFileTimesCMA(
    const char      *name,
    const __time32_t    u_ctime_t,
    const __time32_t    u_mtime_t,
    const __time32_t    u_atime_t);

int _GetUTCFileTimes(
    const char                  *name,
    __time32_t                      *u_atime_t,
    __time32_t                      *u_mtime_t,
    __time32_t                      *u_ctime_t);

BOOL _FileTimeToUnixTime(
    const FILETIME  *ft,
    __time32_t          *ut,
    const BOOL      ft_is_local);

BOOL _UnixTimeToFileTime(
	const __time32_t    ut,
	FILETIME        *ft,
	const BOOL      make_ft_local);

// Cryptic's replacement for _stat32i64(), that avoids timezone bugs and always returns UTC.
int cryptic_stat32i64_utc(const char *name, struct _stat32i64 *buf);

#endif
