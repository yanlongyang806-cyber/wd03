/*------------------------------------------------------------------------------
 * Copyright (c) 2003, Steve Hay. All rights reserved.
 * Portions Copyright (c) 2001, Jonathan M Gilligan. Used with permission.
 * Portions Copyright (c) 2001, Tony M Hoyle. Used with permission.
 *
 * Module Name: Win32::UTCFileTime
 * Source File: UTCFileTime.xs
 * Description: C and XS code for xsubpp
 *------------------------------------------------------------------------------
 */

/*------------------------------------------------------------------------------
 *
 * C code to be copied verbatim by xsubpp.
 */
#include "winfiletime.h"
#include "wininclude.h"                 /* For the Win32 API stuff.         */

#if !_PS3
#include <ctype.h>                      /* For isalpha() and tolower().     */
#include <direct.h>                     /* For _getdrive().                 */
#include <errno.h>                      /* For EACCES.                      */
#include <fcntl.h>                      /* For the O_* flags.               */
#include <io.h>                         /* For lowio and _get_osfhandle().  */
#include <memory.h>                     /* For memset().                    */
#include <stdlib.h>                     /* For errno and tolower().         */
#include <string.h>                     /* For the str*() functions.        */
#include <sys/types.h>                  /* For struct stat.                 */
#include <sys/stat.h>                   /* For struct stat.                 */
#endif

#include <mbstring.h>
#include "file.h"
#include <share.h>
#include "timing.h"
#include "utils.h"
#include "UTF8.h"

#define _Debug() (0)
#define _MAX_FS      32
#define _isslash(c)  ((c) == '\\' || (c) == '/')
#define _isutcfs(fs) (!strstr(fs, "FAT"))

void warn(char *s) {}
void croak(char *s) {}

/*
 * Function to determine whether or not the operating system platform is Windows
 * NT (as opposed to Win32s, Windows [95/98/ME] or Windows CE).
 */
static BOOL _IsWinNT(void) {
#if !PLATFORM_CONSOLE
    static BOOL     initialised = FALSE;
    static BOOL     is_winnt;
    OSVERSIONINFO   osver;

    if (!initialised) {
        ZeroStruct(&osver);
        osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        if (GetVersionEx(&osver)) {
            is_winnt = (osver.dwPlatformId == VER_PLATFORM_WIN32_NT);
        }
        else {
            warn("Could not determine operating system platform. Assuming "
                "the platform is Windows NT");
            is_winnt = TRUE;
        }
        initialised = TRUE;
    }

    return is_winnt;
#else
	return TRUE;
#endif
}

/*
 * Function to determine whether or not file times are stored as UTC in the
 * filesystem that a given file is stored in.
 *
 * This function is based on code written by Tony M Hoyle.
 */

static BOOL _IsUTCVolume(
    const char  *name)
{
#if _PS3
    return TRUE;
#else
    int         len;
    char *szFs = NULL;

	estrStackCreate(&szFs);

    len = (int)strlen(name);

    if (len >= 2 && isalpha((unsigned char)name[0]) && name[1] == ':') {
        /* An absolute path with a drive letter is specified. */
        char root[4] = "?:\\";
        root[0] = name[0];

        if (GetVolumeInformation_UTF8(root, NULL, NULL, NULL, NULL, &szFs))
        {
			bool bRetVal = _isutcfs(szFs);
			estrDestroy(&szFs);

            return bRetVal;
        }
        else {
            warn("Could not determine name of filesystem. Assuming file times "
                "are stored as UTC-based values");
           estrDestroy(&szFs);
		   return TRUE;
        }
    }
    else if (len >= 5 && _isslash(name[0]) && _isslash(name[1])) {
        /* An absolute path with a UNC share (the minimum length of which is 5,
         * as in \\x\y) is specified. We assume that the filesystem is NTFS, and
         * hence stores UTC file times, in this case. */
        estrDestroy(&szFs);
		return TRUE;
    }
    else {
        /* A relative path, or something invalid, is specified. We examine the
         * filesystem of the current directory in this case. */
        if (GetVolumeInformation_UTF8(NULL, NULL, NULL, NULL, NULL, &szFs))
        {
            bool bRetVal =  _isutcfs(szFs);
			estrDestroy(&szFs);
			return bRetVal;
        }
        else {
            warn("Could not determine name of filesystem. Assuming file times "
                "are stored as UTC-based values");
			estrDestroy(&szFs);
            return TRUE;
        }
    }
#endif
}

/*
 * Function to determine whether or not a given year is a leap year, according
 * to the standard Gregorian rule (namely, every year divisible by 4 except
 * centuries indivisble by 400).
 *
 * This function is based on code written by Jonathan M Gilligan.
 */

static BOOL _IsLeapYear(
    WORD year)
{
    return ( ((year & 3u) == 0) &&
             ((year % 100u == 0) || (year % 400u == 0)) );
}

static const WORD days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * Function to compare a test date against a target date. The target date must
 * be specified in the day-in-month format, rather than the absolute format,
 * used by the StandardDate and DaylightDate members of a TIME_ZONE_INFORMATION
 * structure.
 * If the test date is earlier than the target date, it returns a negative
 * number. If the test date is later than the target date, it returns a positive
 * number. If the test date equals the target date, it returns zero.
 * Specifically, it returns:
 * -4/+4 if the test month is less than/greater than the target month;
 * -2/+2 if the test day   is less than/greater than the target day;
 * -1/+1 if the test time  is less than/greater than the target time;
 *   0   if the test date            equals          the target date.
 *
 * This function is based on code written by Jonathan M Gilligan.
 */

static int _CompareTargetDate(
    const SYSTEMTIME *test_st,
    const SYSTEMTIME *target_st)
{
    /* Check that the given dates are in the correct format. */
    if (test_st->wYear == 0)
        croak("The test date used in a date comparison is not in the "
              "required \"absolute\" format");
    if (target_st->wYear != 0)
        croak("The target date used in a date comparison is not in the "
              "required \"day-in-month\" format");

    if (test_st->wMonth != target_st->wMonth) {
        /* The months are different. */

        return (test_st->wMonth > target_st->wMonth) ? 4 : -4;
    }
    else {
        /* The months are the same. */

        WORD    first_dow;
        WORD    temp_dom;
        WORD    last_dom;
        int     test_ms;
        int     target_ms;

        /* If w is the day-of-the-week of some arbitrary day-of-the-month x then
         * the day-of-the-week of the first day-of-the-month is given by
         * ((1 + w - x) mod 7). */
        first_dow = (WORD)((1u + test_st->wDayOfWeek - test_st->wDay) % 7u);

        /* If y is the day-of-the-week of the first day-of-the-month then
         * the day-of-the-month of the first day-of-the-week z is given by
         * ((1 + z - y) mod 7). */
        temp_dom = (WORD)((1u + target_st->wDayOfWeek - first_dow) % 7u);

        /* If t is the day-of-the-month of the first day-of-the-week z then
         * the day-of-the-month of the (n)th day-of-the-week z is given by
         * (t + (n - 1) * 7). */
        temp_dom = (WORD)(temp_dom + target_st->wDay * 7u);

        /* We need to handle the special case of the day-of-the-month of the
         * last day-of-the-week z. For example, if we tried to calculate the
         * day-of-the-month of the fifth Tuesday of the month then we may have
         * overshot, and need to correct for that case.
         * Get the last day-of-the-month (with a suitable correction if it is
         * February of a leap year) and move the temp_dom that we have
         * calculated back one week at a time until it doesn't exceed that. */
		assert((target_st->wMonth > 0) && (target_st->wMonth <= 12));

        last_dom = days_in_month[target_st->wMonth - 1];
        if (test_st->wMonth == 2 && _IsLeapYear(test_st->wYear))
            ++last_dom;
        while (temp_dom > last_dom)
            temp_dom -= 7;

        if (test_st->wDay != temp_dom) {
            /* The days are different. */

            return (test_st->wDay > temp_dom) ? 2 : -2;
        }
        else {
            /* The days are the same. */

            test_ms = ((test_st->wHour     * 60   +
                        test_st->wMinute)  * 60   +
                       test_st->wSecond  ) * 1000 +
                      test_st->wMilliseconds;
            target_ms = ((target_st->wHour     * 60   +
                          target_st->wMinute)  * 60   +
                         target_st->wSecond  ) * 1000 +
                        target_st->wMilliseconds;
            test_ms -= target_ms;
            return (test_ms > 0) ? 1 : (test_ms < 0) ? -1 : 0;
        }
    }
}

/*
 * Function to return the time zone bias for a given local time. The bias is the
 * difference, in minutes, between UTC and local time: UTC = local time + bias.
 *
 * This function is based on code written by Jonathan M Gilligan.
 */

int _GetTimeZoneBias(
    const SYSTEMTIME *st)
{
#if _PS3
    //YVS
    return 0;
#else
    TIME_ZONE_INFORMATION   tz;
    int                     bias;

    if (GetTimeZoneInformation(&tz) == TIME_ZONE_ID_INVALID)
        croak("Could not get time zone information");

    /* We only deal with cases where the transition dates between standard time
     * and daylight time are given in "day-in-month" format rather than
     * "absolute" format. */
    if (tz.DaylightDate.wYear != 0 || tz.StandardDate.wYear != 0)
        croak("Cannot handle year-specific DST clues in time zone information");

    /* Get the difference between UTC and local time. */
    bias = tz.Bias;

    /* Add on the standard bias (usually 0) or the daylight bias (usually -60)
     * as appropriate for the given time. */
    if (_CompareTargetDate(st, &tz.DaylightDate) < 0) {
        bias += tz.StandardBias;
    }
    else if (_CompareTargetDate(st, &tz.StandardDate) < 0) {
        bias += tz.DaylightBias;
    }
    else {
        bias += tz.StandardBias;
    }

    return bias;
#endif
}

#if !_PS3
/* Number of "clunks" (100-nanosecond intervals) in one second. */
static const ULONGLONG  clunks_per_second = 10000000L;

/* The epoch of __time32_t values (00:00:00 Jan 01 1970 UTC) as a SYSTEMTIME. */
static const SYSTEMTIME base_st = {
    1970,   /* wYear            */
    1,      /* wMonth           */
    0,      /* wDayOfWeek       */
    1,      /* wDay             */
    0,      /* wHour            */
    0,      /* wMinute          */
    0,      /* wSecond          */
    0       /* wMilliseconds    */
};
#endif

/*
 * Function to convert a FILETIME to a __time32_t.
 * The __time32_t will be UTC-based, so if the FILETIME is local time-based then
 * set the ft_is_local flag so that a local time adjustment can be made.
 *
 * This function is based on code written by Jonathan M Gilligan.
 */

BOOL _FileTimeToUnixTime(
    const FILETIME  *ft,
    __time32_t      *ut,
    const BOOL      ft_is_local)
{
#if _PS3
    *ut = *ft;
#else
    int             bias = 0;
    static BOOL     initialised = FALSE;
    static FILETIME base_ft;
    ULARGE_INTEGER  it;

    if (ft_is_local) {
        SYSTEMTIME  st;

        /* Convert the FILETIME to a SYSTEMTIME, and get the bias from that. */
        if (FileTimeToSystemTime(ft, &st)) {
            bias = _GetTimeZoneBias(&st);
        }
        else {
            if (_Debug())
                warn("FileTimeToSystemTime() failed\n");

            /* Do the same as mktime() in the event of failure. */
            *ut = -1;
            return FALSE;
        }
    }

    /* Get the epoch of __time32_t values as a FILETIME. This calculation only needs
     * to be done once. */
    if (!initialised) {
        if (!SystemTimeToFileTime(&base_st, &base_ft))
            croak("Could not convert base SYSTEMTIME to FILETIME");
        initialised = TRUE;
    }

    /* Convert the FILETIME (which is expressed as the number of clunks
     * since 00:00:00 Jan 01 1601 UTC) to a __time32_t value by subtracting the
     * FILETIME representation of the epoch of __time32_t values and then
     * converting clunks to seconds. */
    it.QuadPart  = ((ULARGE_INTEGER *)ft)->QuadPart;
    it.QuadPart -= ((ULARGE_INTEGER *)&base_ft)->QuadPart;
    it.QuadPart /= clunks_per_second;

    /* Add the bias (which is in minutes) to get UTC. */
    it.QuadPart += bias * 60;

    *ut = it.LowPart;
#endif
    return TRUE;
}

/*
 * Function to convert a __time32_t to a FILETIME.
 * The __time32_t is UTC-based, so if a local time-based FILETIME is required then
 * set the make_ft_local flag so that a local time adjustment can be made.
 *
 * This function is based on code written by Tony M Hoyle.
 */

BOOL _UnixTimeToFileTime(
    const __time32_t    ut,
    FILETIME        *ft,
    const BOOL      make_ft_local)
{
#if _PS3
    *ft = ut;
#else
    static BOOL     initialised = FALSE;
    static FILETIME base_ft;
    ULARGE_INTEGER  it;
    int             bias = 0;

    /* Get the epoch of __time32_t values as a FILETIME. This calculation only needs
     * to be done once. */
    if (!initialised) {
        if (!SystemTimeToFileTime(&base_st, &base_ft))
            croak("Could not convert base SYSTEMTIME to FILETIME");
        initialised = TRUE;
    }

    /* Convert the __time32_t value to a FILETIME (which is expressed as the
     * number of clunks since 00:00:00 Jan 01 1601 UTC) by converting
     * seconds to clunks and then adding the FILETIME representation of the
     * epoch of __time32_t values. */
    it.LowPart   = ut;
    it.HighPart  = 0;
    it.QuadPart *= clunks_per_second;
    it.QuadPart += ((ULARGE_INTEGER *)&base_ft)->QuadPart;

    if (make_ft_local) {
        SYSTEMTIME  st;

        /* Convert the FILETIME to a SYSTEMTIME, and get the bias from that. */
        if (FileTimeToSystemTime((FILETIME *)&it, &st)) {
            bias = _GetTimeZoneBias(&st);
        }
        else {
            if (_Debug())
                warn("FileTimeToSystemTime() failed\n");

            /* Set a zero FILETIME in the event of failure. */
            (*ft).dwLowDateTime  = 0;
            (*ft).dwHighDateTime = 0;
            return FALSE;
        }
    }

    /* Add the bias (which is in minutes) to get UTC. */
    it.QuadPart += bias * 60;

    *(ULARGE_INTEGER *)ft = it;
#endif
    return TRUE;
}

/*
 * Function to convert three FILETIME values (the last access time, last
 * modification time and creation time of a given file from calls to either
 * FindFirstFile() or GetFileInformationByHandle()) to __time32_t values.
 * The __time32_t values will be UTC-based, whatever filesystem the file is stored
 * in.
 *
 * This function is based on code written by Jonathan M Gilligan.
 */

static BOOL _FileTimesToUnixTimes(
    const char      *name,
    const FILETIME  *atime_ft,
    const FILETIME  *mtime_ft,
    const FILETIME  *ctime_ft,
    __time32_t          *u_atime_t,
    __time32_t          *u_mtime_t,
    __time32_t          *u_ctime_t)
{
    BOOL            ret;

    /* The filesystem stores UTC file times. FindFirstFile() and
     * GetFileInformationByHandle() return them to us as unadulterated UTC
     * FILETIMEs, so just convert them to __time32_t values to be returned. */
    ret =   (!u_atime_t || _FileTimeToUnixTime(atime_ft, u_atime_t, FALSE)) &&
            (!u_mtime_t || _FileTimeToUnixTime(mtime_ft, u_mtime_t, FALSE)) &&
            (!u_ctime_t || _FileTimeToUnixTime(ctime_ft, u_ctime_t, FALSE));

    return ret;
}

/*
 * Function to convert file attributes as returned by the Win32 API function
 * GetFileAttributes() into a Unix mode as stored in the st_mode field of a
 * struct stat.
 */

static unsigned short _FileAttributesToUnixMode(
    const DWORD    fa,
    const char     *name)
{
    unsigned short st_mode = 0;
    int            len;
    const char     *p;

    if (fa & FILE_ATTRIBUTE_DIRECTORY)
        st_mode |= _S_IFDIR;
    else
        st_mode |= _S_IFREG;

    if (fa & FILE_ATTRIBUTE_READONLY)
        st_mode |= (  _S_IREAD       +
                     (_S_IREAD >> 3) +
                     (_S_IREAD >> 6));
    else
        st_mode |= ( (_S_IREAD | _S_IWRITE)       +
                    ((_S_IREAD | _S_IWRITE) >> 3) +
                    ((_S_IREAD | _S_IWRITE) >> 6));

    if (fa & FILE_ATTRIBUTE_DIRECTORY)
        st_mode |= (  _S_IEXEC       +
                     (_S_IEXEC >> 3) +
                     (_S_IEXEC >> 6));

    len = (int)strlen(name);
    if (len >= 4 && (*(p = name + len - 4) == '.') &&
            (!stricmp(p, ".exe") ||  !stricmp(p, ".bat") ||
             !stricmp(p, ".com") || (!stricmp(p, ".cmd")
			 && _IsWinNT()
			 )))
        st_mode |= (  _S_IEXEC       +
                     (_S_IEXEC >> 3) +
                     (_S_IEXEC >> 6));

    return st_mode;
}

/*
 * Function to emulate the standard C library function stat(), setting the
 * last access time, last modification time and creation time members of the
 * given "stat" structure to UTC-based __time32_t values, whatever filesystem the
 * file is stored in.
 *
 * This function is based on code in CVSNT's wnt_stat() function, and on code in
 * Perl's win32_stat() function.
 */

int _AltStat(
    const char                  *name,
    struct _stat32i64           *st_buf)
{
    int                         drive = 1;
    HANDLE                      hndl;
    BY_HANDLE_FILE_INFORMATION  bhfi;
#ifdef _XBOX
	char	namebuf[MAX_PATH];
	strcpy(namebuf, name);
	backSlashes(namebuf);
	name = namebuf;
#endif

    PERFINFO_AUTO_START_BLOCKING("_AltStat", 1);

	fileDiskAccessCheck();
    
    ZeroStruct(&bhfi);

	//OutputDebugStringf("AltStat: %s\n", name);

    /* Use CreateFile(), rather than FindFirstFile() like Microsoft's stat()
     * does, for three reasons:
     * (1) It doesn't require "List Folder Contents" permission on the parent
     *     directory like FindFirstFile() does;
     * (2) It works for directories specified with a trailing slash or backslash
     *     and it works for root (drive or UNC) directories like C: and
     *     \\SERVER\SHARE, with or without a trailing slash or backslash
     *     (provided that this is a Windows NT platform and the
     *     FILE_FLAG_BACKUP_SEMANTICS flag is passed to allow directory handles
     *     to be obtained), whereas FindFirstFile() requires non-root
     *     directories to not have a trailing slash or backslash and requires
     *     root directories to have a trailing \*; and
     * (3) The BY_HANDLE_FILE_INFORMATION stucture returned by a subsequent call
     *     to GetFileInformationByHandle() contains the number of links to the
     *     file, which the WIN32_FIND_DATA structure returned by FindFirstFile()
     *     does not. */
    if ((hndl = CreateFile_UTF8(name,
                    GENERIC_READ,
                    FILE_SHARE_READ|FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    NULL)) == INVALID_HANDLE_VALUE)
    {
        /* If this is a valid directory (presumably under a Windows 95 platform
         * on which the FILE_FLAG_BACKUP_SEMANTICS flag doesn't do the trick)
         * then set all the fields except st_mode to zero and return TRUE, like
         * Perl's built-in functions do in this case. */
        DWORD fa = GetFileAttributes_UTF8(name);
        if (fa != 0xFFFFFFFF && (fa & FILE_ATTRIBUTE_DIRECTORY)) {
            ZeroStruct(st_buf);
            st_buf->st_mode = _FileAttributesToUnixMode(fa, name);
            PERFINFO_AUTO_STOP();
            return 0;
        }
        else {
            PERFINFO_AUTO_STOP();
            return -1;
        }
    }
    else {
        GetFileInformationByHandle(hndl, &bhfi);
        if (!CloseHandle(hndl))
            warn("Could not close file object handle");
    }

    if (!_FileTimesToUnixTimes(name,
            &bhfi.ftLastAccessTime, &bhfi.ftLastWriteTime, &bhfi.ftCreationTime,
            NULL, &st_buf->st_mtime, NULL))
    {
		PERFINFO_AUTO_STOP();
        return -1;
    }

    st_buf->st_mode = _FileAttributesToUnixMode(bhfi.dwFileAttributes, name);

	st_buf->st_nlink = (short)bhfi.nNumberOfLinks;

    st_buf->st_size = bhfi.nFileSizeLow + (((U64)bhfi.nFileSizeHigh) << 32LL);

    /* Get the drive from the name, or use the current drive. */
    if (strlen(name) >= 2 && isalpha((unsigned char)name[0]) && name[1] == ':')
        drive = tolower(name[0]) - 'a' + 1;
#ifndef _XBOX
    else
        drive = _getdrive();
#endif

    st_buf->st_dev = st_buf->st_rdev = (_dev_t)(drive - 1);

    st_buf->st_ino = st_buf->st_uid = st_buf->st_gid = 0;

	PERFINFO_AUTO_STOP();

    return 0;
}

/*
 * Function to get the last access time, last modification time and creation
 * time of a given file.
 * The values are returned expressed as UTC-based __time32_t values, whatever
 * filesystem the file is stored in.
 *
 * This function is based on code written by Jonathan M Gilligan.
 */

int _GetUTCFileTimes(
    const char                  *name,
    __time32_t                      *u_atime_t,
    __time32_t                      *u_mtime_t,
    __time32_t                      *u_ctime_t)
{
    HANDLE                      hndl;
    WIN32_FIND_DATAA             wfd;
    BY_HANDLE_FILE_INFORMATION  bhfi;

    /* Use FindFirstFile() like Microsoft's stat() does, rather than the more
     * obvious GetFileTime(), to avoid a problem with the latter caching UTC
     * time values on FAT volumes. */
    if ((hndl = FindFirstFile_UTF8(name, &wfd)) == INVALID_HANDLE_VALUE) {
        /* FindFirstFile() will fail if the given name specifies a directory
         * with a trailing slash or backslash, or if it is a root (drive or UNC)
         * directory like C: or \\SERVER\SHARE. CreateFile() does not have these
         * restrictions (provided that this is a Windows NT platform and the
         * FILE_FLAG_BACKUP_SEMANTICS flag is passed to allow directory handles
         * to be obtained), so try that instead if FindFirstFile() failed. */
        if ((hndl = CreateFile_UTF8(name,
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS,
                        NULL)) == INVALID_HANDLE_VALUE)
        {
            /* This function is only ever called after a call to Perl's built-in
             * stat() or lstat() function has already succeeded on the same
             * name, so this must just be a directory under a Windows 95
             * platform on which the FILE_FLAG_BACKUP_SEMANTICS flag doesn't do
             * the trick. Set all the file times to zero and return TRUE, like
             * Perl's built-in functions do in this case. */
            if(u_atime_t){
				*u_atime_t = 0;
			}
            if(u_mtime_t){
				*u_mtime_t = 0;
			}
            if(u_ctime_t){
				*u_ctime_t = 0;
			}
            return TRUE;
        }
        else {
            GetFileInformationByHandle(hndl, &bhfi);
            if (!CloseHandle(hndl))
                warn("Could not close file object handle");
            wfd.ftLastAccessTime = bhfi.ftLastAccessTime;
            wfd.ftLastWriteTime  = bhfi.ftLastWriteTime;
            wfd.ftCreationTime   = bhfi.ftCreationTime;
        }
    }
    else {
        if (!FindClose(hndl))
            warn("Could not close file search handle");
    }

    return _FileTimesToUnixTimes(name,
            &wfd.ftLastAccessTime, &wfd.ftLastWriteTime, &wfd.ftCreationTime,
            u_atime_t, u_mtime_t, u_ctime_t);
}
// #endif

/*
 * Function to set the cration, last modification and last access time of a given
 * file.
 * The values should be supplied expressed as UTC-based __time32_t values, whatever
 * filesystem the file is stored in.
 */

int _SetUTCFileTimesCMA(
    const char      *name,
    const __time32_t    u_ctime_t,
    const __time32_t    u_mtime_t,
    const __time32_t    u_atime_t)
{
    int             fd;
    BOOL            ret = FALSE;
    HANDLE          hndl;

    /* Try opening the file normally first, like Microsoft's utime(), and hence
     * Perl's win32_utime(), does. Note that this will fail with errno EACCES if
     * name specifies a directory or a read-only file. */
    if ((fd = open(name, O_RDWR | O_BINARY)) < 0) {
        if (errno == EACCES) {
            /* CreateFile() can open directory handles (provided that this is a
             * Windows NT platform and the FILE_FLAG_BACKUP_SEMANTICS flag is
             * passed to allow directory handles to be obtained), so try that
             * instead like Perl's win32_utime() does. This will (and should)
             * still fail on read-only files. */
            if ((hndl = CreateFile_UTF8(name,
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_DELETE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS,
                            NULL)) == INVALID_HANDLE_VALUE)
            {
                return FALSE;
            }
        }
        else {
            return FALSE;
        }
    }
    else {
        hndl = (HANDLE)_get_osfhandle(fd);
    }

	/* Use NULL for the creation time passed to SetFileTime() like Microsoft's
	 * utime() does. This simply means that the information is not changed.
	 * There is no need to retrieve the existing value first in order to reset
	 * it like Perl's win32_utime() does. */
	{
		FILETIME    u_atime_ft;
		FILETIME    u_mtime_ft;
		FILETIME    u_ctime_ft;

		/* The filesystem stores UTC file times. SetFileTime() will set its UTC
		 * FILETIME arguments without change, so just convert the __time32_t values
		 * to UTC FILETIMEs to be set. */
		if ((!u_atime_t || _UnixTimeToFileTime(u_atime_t, &u_atime_ft, FALSE)) &&
			(!u_mtime_t || _UnixTimeToFileTime(u_mtime_t, &u_mtime_ft, FALSE)) &&
			(!u_ctime_t || _UnixTimeToFileTime(u_ctime_t, &u_ctime_ft, FALSE)))
		{
			if (!SetFileTime(hndl, u_ctime_t ? &u_ctime_ft : NULL, u_atime_t ? &u_atime_ft : NULL, u_mtime_t ? &u_mtime_ft : NULL)) {
				ret = FALSE;
			}
			else {
				ret = TRUE;
			}
		}
		else {
			ret = FALSE;
		}
	}

    if (fd >= 0)
		close(fd);
    return ret;
}

// From the CRT, see cryptic_stat32i64_utc().
#define ISSLASH(a)  ((a) == '\\' || (a) == '/')

// From the CRT, see cryptic_stat32i64_utc().
/*
 * IsRootUNCName - returns TRUE if the argument is a UNC name specifying
 *      a root share.  That is, if it is of the form \\server\share\.
 *      This routine will also return true if the argument is of the
 *      form \\server\share (no trailing slash) but Win32 currently
 *      does not like that form.
 *
 *      Forward slashes ('/') may be used instead of backslashes ('\').
 */
static int cryptic_IsRootUNCName(const char *path)
{
    /*
     * If a root UNC name, path will start with 2 (but not 3) slashes
     */

    if ( ( strlen ( path ) >= 5 ) /* minimum string is "//x/y" */
         && ISSLASH(path[0]) && ISSLASH(path[1])
         && !ISSLASH(path[2]))
    {
        const char * p = path + 2 ;

        /*
         * find the slash between the server name and share name
         */
        while ( * ++ p )
        {
            if ( ISSLASH(*p) )
            {
                break;
            }
        }

        if ( *p && p[1] )
        {
            /*
             * is there a further slash?
             */
            while ( * ++ p )
            {
                if ( ISSLASH(*p) )
                {
                    break;
                }
            }

            /*
             * just final slash (or no final slash)
             */
            if ( !*p || !p[1])
            {
                return 1;
            }
        }
    }

    return 0 ;
}

// From the CRT, see cryptic_stat32i64_utc().
static char * cryptic_tfullpath_helper(char * buf,const char *path,size_t sz, char ** pBuf)
{
    char * ret;
    errno_t save_errno = errno;

    errno = 0;
    ret = fullpath_UTF8(buf, path, (int)sz);
    if (ret)
    {
        errno = save_errno;
        return ret;
    }

    /* if _tfullpath fails because buf is too small, then we just call again _tfullpath and
     * have it allocate the appropriate buffer
     */
    if (errno != ERANGE)
    {
        /* _tfullpath is failing for another reason, just propagate the failure and keep the
         * failure code in errno
         */
        return NULL;
    }
    errno = save_errno;

    *pBuf = fullpath_UTF8(NULL, path, 0);

    return *pBuf;
}

// From the CRT, see cryptic_stat32i64_utc().
static unsigned short __cdecl cryptic_tdtoxmode (
    int attr,
    const char *name
    )
{
    unsigned short uxmode;
    unsigned dosmode;
    const char *p;

    dosmode = attr & 0xff;
    if ((p = name)[1] == ':')
        p += 2;

    /* check to see if this is a directory - note we must make a special
    * check for the root, which DOS thinks is not a directory
    */

    uxmode = (unsigned short)
             (((ISSLASH(*p) && !p[1]) || (dosmode & 0x10) || !*p)
             ? _S_IFDIR|_S_IEXEC : _S_IFREG);

    /* If attribute byte does not have read-only bit, it is read-write */

    uxmode |= (dosmode & 0x1) ? _S_IREAD : (_S_IREAD|_S_IWRITE);

    /* see if file appears to be executable - check extension of name */

    if (p = strrchr(name, '.')) {
        if ( !stricmp(p, ".exe") ||
             !stricmp(p, ".cmd") ||
             !stricmp(p, ".bat") ||
             !stricmp(p, ".com") )
            uxmode |= _S_IEXEC;
    }

    /* propagate user read/write/execute bits to group/other fields */

    uxmode |= (uxmode & 0700) >> 3;
    uxmode |= (uxmode & 0700) >> 6;

    return(uxmode);
}

// Cryptic's replacement for _stat32i64(), that avoids timezone bugs and always returns UTC.
// This is based on the _stat32i64() implementation in stat.c in the MSVS2010 SP1 CRT.
// The code is the same, except for the following:
//   -Use the CVS/Perl method for getting time in UTC instead of the buggy __loctotime32_t method in the CRT
//   -Cryptic perftimers
//   -Hacks for the symlink case
//   -Miscellaneous changes to make it compile
/***
*int _stat(name, buf) - get file status info
*
*Purpose:
*       _stat obtains information about the file and stores it in the
*       structure pointed to by buf.
*
*       Note: We cannot directly use the file time stamps returned in the
*       WIN32_FIND_DATA structure. The values are supposedly in system time
*       and system time is ambiguously defined (it is UTC for Windows NT, local
*       time for Win32S and probably local time for Win32C). Therefore, these
*       values must be converted to local time before than can be used.
*
*Entry:
*       _TSCHAR *name -    pathname of given file
*       struct _stat *buffer - pointer to buffer to store info in
*
*Exit:
*       fills in structure pointed to by buffer
*       returns 0 if successful
*       returns -1 and sets errno if unsuccessful
*
*Exceptions:
*
*******************************************************************************/
int cryptic_stat32i64_utc (
    const char *name,
    struct _stat32i64 *buf
    )
{
    char *  path;
    char    pathbuf[ _MAX_PATH ];
    int        drive; /* A: = 1, B: = 2, etc. */
    HANDLE     findhandle;
    WIN32_FIND_DATA findbuf;
    int retval = 0;

	PERFINFO_AUTO_START_BLOCKING("cryptic_stat32i64_utc", 1);

	if(name == NULL) {errno = EINVAL; return -1;}
	if(buf == NULL) {errno = EINVAL; return -1;}

    /* Don't allow wildcards to be interpreted by system */
    if (_mbspbrk(name, "?*"))
    {
        errno = ENOENT;
		_doserrno = 2;
		PERFINFO_AUTO_STOP();
        return(-1);
    }

    /* Try to get disk from name.  If none, get current disk.  */

    if (name[1] == ':')
    {
        if ( *name && !name[2] )
        {
            errno = ENOENT;             /* return an error if name is   */
			_doserrno = 2;
			PERFINFO_AUTO_STOP();
            return( -1 );
        }
        drive = _tolower(*name) - 'a' + 1;
    }
    else
    {
        drive = _getdrive();
    }

    /* Call Find Match File */
    findhandle = FindFirstFileEx_UTF8(name, FindExInfoStandard, &findbuf, FindExSearchNameMatch, NULL, 0);
    if ( findhandle == INVALID_HANDLE_VALUE )
    {
        char * pBuf = NULL;

        if ( !( _mbspbrk(name, "./\\") &&

             (path = cryptic_tfullpath_helper( pathbuf, name, _MAX_PATH, &pBuf )) &&
             /* root dir. ('C:\') or UNC root dir. ('\\server\share\') */
             ((strlen( path ) == 3) || cryptic_IsRootUNCName(path)) &&
             (GetDriveType_UTF8( path ) > 1) ) )
        {
            if(pBuf)
                free(pBuf);

            errno = ENOENT;
			_doserrno = 2;
			PERFINFO_AUTO_STOP();
            return( -1 );
        }

        if(pBuf)
        {
            free(pBuf);
        }

        /*
         * Root directories (such as C:\ or \\server\share\ are fabricated.
         */

        findbuf.dwFileAttributes = 0x10;
        findbuf.nFileSizeHigh = 0;
        findbuf.nFileSizeLow = 0;
        findbuf.cFileName[0] = '\0';

        buf->st_mtime = 0;
        buf->st_atime = buf->st_mtime;
        buf->st_ctime = buf->st_mtime;
    }
    else if ( (findbuf.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
              (findbuf.dwReserved0 == IO_REPARSE_TAG_SYMLINK) )
    {
        /* if the file is a symbolic link, then use fstat to fill the info in the _stat struct */
        int fd = -1;
        errno_t e;

        e = _wsopen_s_UTF8(&fd, name, _O_RDONLY, _SH_DENYNO, 0 /* ignored */);
        if (e != 0 || fd == -1)
        {
            errno = ENOENT;
            _doserrno = 2;
			PERFINFO_AUTO_STOP();
            return -1;
        }

        retval = _fstat32i64(fd, buf);  // FIXME: For the symlink case, the times might be off.
		buf->st_mtime = statTimeToUTC(buf->st_mtime);
		buf->st_atime = statTimeToUTC(buf->st_atime);
		buf->st_ctime = statTimeToUTC(buf->st_ctime);
        _close(fd);

		PERFINFO_AUTO_STOP();
        return retval;
    }
    else
    {
        if ( findbuf.ftLastWriteTime.dwLowDateTime ||
             findbuf.ftLastWriteTime.dwHighDateTime )
        {
			bool result = _FileTimeToUnixTime(&findbuf.ftLastWriteTime, &buf->st_mtime, false);
			if (!result)
			{
				_doserrno = EFAULT;
				FindClose( findhandle );
				PERFINFO_AUTO_STOP();
				return( -1 );
			}
        }
        else
        {
            buf->st_mtime = 0;
        }

        if ( findbuf.ftLastAccessTime.dwLowDateTime ||
             findbuf.ftLastAccessTime.dwHighDateTime )
        {
			bool result  = _FileTimeToUnixTime(&findbuf.ftLastAccessTime, &buf->st_atime, false);
			if (!result)
			{
				_doserrno = EFAULT;
				FindClose( findhandle );
				PERFINFO_AUTO_STOP();
				return( -1 );
			}
        }
        else
        {
            buf->st_atime = buf->st_mtime;
        }

        if ( findbuf.ftCreationTime.dwLowDateTime ||
             findbuf.ftCreationTime.dwHighDateTime )
        {
			bool result  = _FileTimeToUnixTime(&findbuf.ftCreationTime, &buf->st_ctime, false);
			if (!result)
			{
				_doserrno = EFAULT;
				FindClose( findhandle );
				PERFINFO_AUTO_STOP();
				return( -1 );
			}
        }
        else
        {
            buf->st_ctime = buf->st_mtime;
        }

        FindClose(findhandle);
    }

    /* Fill in buf */

    buf->st_mode = cryptic_tdtoxmode(findbuf.dwFileAttributes, name);
    buf->st_nlink = 1;

    buf->st_size = ((__int64)(findbuf.nFileSizeHigh)) * (0x100000000i64) +
                    (__int64)(findbuf.nFileSizeLow);

    /* now set the common fields */

    buf->st_uid = buf->st_gid = buf->st_ino = 0;

    buf->st_rdev = buf->st_dev = (_dev_t)(drive - 1); /* A=0, B=1, etc. */

	PERFINFO_AUTO_STOP();
    return retval;
}
